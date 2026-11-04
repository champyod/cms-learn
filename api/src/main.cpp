#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <print>
#include <string>
#include <string_view>
#include <vector>

#include <drogon/drogon.h>

#include <cms/api/admin_socket.hpp>
#include <cms/api/auth_routes.hpp>
#include <cms/api/authorize.hpp>
#include <cms/api/form_body.hpp>
#include <cms/api/host_config.hpp>
#include <cms/api/page_routes.hpp>
#include <cms/api/settings_route.hpp>
#include <cms/api/settings_source.hpp>
#include <cms/api/settings_view.hpp>
#include <cms/api/settings_watcher.hpp>
#include <cms/api/submissions.hpp>
#include <cms/api/task_view.hpp>
#include <cms/capability.hpp>
#include <cms/db/connection.hpp>
#include <cms/settings_snapshot.hpp>

namespace {

constexpr std::size_t DATABASE_CONNECTIONS = 4;
/// The backstop for a notification that never arrived. A policy value, named rather than inlined.
constexpr unsigned SETTINGS_POLL_SECONDS = 30;

auto fail(std::string_view message) -> int {
  try {
    std::println(stderr, "cms-api: {}", message);
  } catch (...) {
    // The failure path must never itself fail. std::println can throw format_error, and this is called
    // from the catch block in main, so a broken format falls back to a write that cannot throw.
    constexpr std::string_view PREFIX = "cms-api: ";
    std::fwrite(PREFIX.data(), 1, PREFIX.size(), stderr);
    std::fwrite(message.data(), 1, message.size(), stderr);
    std::fputc('\n', stderr);
  }
  return 1;
}

auto load_from_environment() -> cms::Result<cms::api::HostConfig> {
  const auto* path = std::getenv("CMSLEARN_HOST_FILE");
  if (path == nullptr || std::strlen(path) == 0) {
    return std::unexpected(cms::Error::make(
        cms::ErrorCode::invalid_argument,
        "CMSLEARN_HOST_FILE is not set; this service takes every fact from the file it names"));
  }
  return cms::api::load_host_config(path);
}

auto text_response(std::string body, drogon::HttpStatusCode status = drogon::k200OK)
    -> drogon::HttpResponsePtr {
  auto response = drogon::HttpResponse::newHttpResponse();
  response->setStatusCode(status);
  response->setContentTypeString("text/plain; charset=utf-8");
  response->setBody(std::move(body));
  return response;
}

auto register_routes(const std::shared_ptr<cms::SettingsStore>& store,
                     const std::shared_ptr<const cms::db::connection_config>& database_config,
                     const std::vector<cms::api::CatalogueEntry>& catalogue) -> void {
  auto& app = drogon::app();

  app.registerHandler(
      "/healthz",
      [store](const drogon::HttpRequestPtr&,
              std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        callback(text_response("ok settings-version=" + std::to_string(store->version()) + "\n"));
      },
      {drogon::Get});

  app.registerHandler(
      "/metrics",
      [store](const drogon::HttpRequestPtr&,
              std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto body = std::string{};
        body += "# HELP cms_learn_settings_version The settings row version this process holds.\n";
        body += "# TYPE cms_learn_settings_version gauge\n";
        body += "cms_learn_settings_version " + std::to_string(store->version()) + "\n";
        callback(text_response(std::move(body)));
      },
      {drogon::Get});

  // The task page posts here. The account comes from the session cookie, never from the body, so a client
  // cannot submit as somebody else by editing a form field. The audience is composed from that same cookie,
  // so a task hidden from the viewer is refused by the insert rather than by a check the handler could miss.
  app.registerHandler(
      "/tasks/{}/submit",
      [database_config](const drogon::HttpRequestPtr& request,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                        std::string_view task_id) {
        auto database = cms::db::connection{database_config};
        const auto audience =
            cms::api::audience_of_session(database, request->getCookie("cms_session"));
        if (!audience) {
          callback(text_response(audience.error().message, drogon::k500InternalServerError));
          return;
        }
        const auto submitted =
            cms::api::submit_solution(database, request->getCookie("cms_session"), task_id,
                                      cms::api::form_field(request->body(), "source"), *audience);

        if (!submitted) {
          callback(text_response(submitted.error().message, drogon::k400BadRequest));
          return;
        }
        const auto location = std::string{"/tasks/"}.append(task_id);
        callback(drogon::HttpResponse::newRedirectionResponse(location, drogon::k303SeeOther));
      },
      {drogon::Post});

  // The settings page posts every field it renders. The account comes from the session cookie, so a client
  // cannot write settings as somebody else by editing a field.
  app.registerHandler(
      "/settings",
      [database_config, catalogue](const drogon::HttpRequestPtr& request,
                                   std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto database = cms::db::connection{database_config};
        const auto principal =
            cms::api::principal_of_session(database, request->getCookie("cms_session"));
        if (!principal) {
          callback(text_response(principal.error().message, drogon::k500InternalServerError));
          return;
        }
        if (principal->empty()) {
          callback(text_response("no live session; sign in before changing settings",
                                 drogon::k403Forbidden));
          return;
        }
        const auto permitted =
            cms::api::authorize(database, *principal, cms::Capability::settings_write);
        if (!permitted) {
          callback(text_response(permitted.error().message, drogon::k403Forbidden));
          return;
        }
        const auto changes = cms::api::setting_changes(request->body(), catalogue);
        const auto written =
            cms::api::commit_settings(database_config, catalogue, changes, *principal);
        if (!written) {
          callback(text_response(written.error().message, drogon::k400BadRequest));
          return;
        }
        callback(drogon::HttpResponse::newRedirectionResponse("/settings", drogon::k303SeeOther));
      },
      {drogon::Post});
}

/// Serves the published assets. One route covers the whole tree, and the path it names is resolved before
/// it is compared with the root, so a request that leaves the tree is refused instead of read.
auto register_asset_route(std::string_view static_root) -> void {
  drogon::app().registerHandlerViaRegex(
      "/static/(.*)",
      [static_root = std::string{static_root}](
          const drogon::HttpRequestPtr&,
          std::function<void(const drogon::HttpResponsePtr&)>&& callback,
          std::string_view relative) {
        const auto file = cms::api::static_file(static_root, relative);
        if (!file) {
          callback(text_response("no such asset\n", drogon::k404NotFound));
          return;
        }
        callback(drogon::HttpResponse::newFileResponse(*file));
      },
      {drogon::Get});
}

/// Runs the service once. Kept separate from main so an exception during startup is reported like any
/// other refusal rather than escaping as std::terminate.
auto run() -> int {
  const auto config = load_from_environment();
  if (!config) {
    return fail(config.error().message);
  }

  const auto client =
      drogon::orm::DbClient::newPgClient(config->database_url, DATABASE_CONNECTIONS);
  const auto database_config = std::make_shared<const cms::db::connection_config>(
      cms::db::connection_config{.client = client});
  auto database = cms::db::connection{database_config};

  const auto snapshot = cms::api::load_settings(database);
  if (!snapshot) {
    return fail(snapshot.error().message);
  }

  const auto catalogue = cms::api::load_catalogue(CMS_CATALOGUE_FILE);
  if (!catalogue) {
    return fail(catalogue.error().message);
  }

  auto store = std::make_shared<cms::SettingsStore>(*snapshot);
  auto settings_view = std::make_shared<cms::api::SettingsView>(*catalogue);
  register_routes(store, database_config, *catalogue);
  cms::api::register_home_route(store, database_config, config->template_root);
  cms::api::register_task_route(store, database_config, config->template_root);
  cms::api::register_settings_route(store, database_config, settings_view,
                                    config->template_root);
  cms::api::register_auth_routes(store, database_config, config->template_root, config->tls_mode);
  register_asset_route(config->static_root);

  auto watcher = cms::api::SettingsWatcher{database, store, config->database_url,
                                           SETTINGS_POLL_SECONDS};
  watcher.start();

  const auto allowed_uid = static_cast<uid_t>(std::stoul(config->admin_uid));
  auto admin = cms::api::AdminSocket{config->admin_socket, allowed_uid, store, database_config,
                                     *catalogue};
  admin.start();

  auto& app = drogon::app();
  app.addListener(config->listen_address,
                  static_cast<std::uint16_t>(std::stoi(config->listen_port)));
  app.run();
  return 0;
}

}  // namespace

int main() {
  try {
    return run();
  } catch (const std::exception& error) {
    return fail(error.what());
  } catch (...) {
    return fail("the service stopped on a failure that is not a std::exception");
  }
}
