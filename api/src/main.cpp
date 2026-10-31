#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <print>
#include <string>
#include <string_view>

#include <drogon/drogon.h>

#include <cms/api/admin_socket.hpp>
#include <cms/api/host_config.hpp>
#include <cms/api/settings_source.hpp>
#include <cms/api/settings_watcher.hpp>
#include <cms/db/connection.hpp>
#include <cms/settings_snapshot.hpp>

namespace {

constexpr std::size_t DATABASE_CONNECTIONS = 4;
/// The backstop for a notification that never arrived. A policy value, so it becomes a settings row
/// once the catalogue is wired; until then it is named rather than inlined.
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

auto register_routes(const std::shared_ptr<cms::SettingsStore>& store) -> void {
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

  auto store = std::make_shared<cms::SettingsStore>(*snapshot);
  register_routes(store);

  auto watcher = cms::api::SettingsWatcher{database, store, config->database_url,
                                           SETTINGS_POLL_SECONDS};
  watcher.start();

  const auto allowed_uid = static_cast<uid_t>(std::stoul(config->admin_uid));
  auto admin = cms::api::AdminSocket{config->admin_socket, allowed_uid, store, database_config};
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
