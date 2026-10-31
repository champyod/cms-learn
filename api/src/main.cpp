#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

#include <drogon/drogon.h>

#include <cms/api/host_config.hpp>
#include <cms/api/settings_source.hpp>
#include <cms/api/settings_watcher.hpp>
#include <cms/db/connection.hpp>
#include <cms/settings_snapshot.hpp>

namespace {

constexpr std::string_view CHECK_FLAG = "--check";
constexpr std::size_t DATABASE_CONNECTIONS = 4;
/// The backstop for a notification that never arrived. A policy value, so it becomes a settings row
/// once the catalogue is wired; until then it is named rather than inlined.
constexpr unsigned SETTINGS_POLL_SECONDS = 30;

auto fail(std::string_view message) -> int {
  std::cerr << "cms-api: " << message << '\n';
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

auto register_routes(std::shared_ptr<cms::SettingsStore> store) -> void {
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

}  // namespace

int main(int argc, char** argv) {
  const auto config = load_from_environment();
  if (!config) {
    return fail(config.error().message);
  }

  if (argc > 1 && std::string_view{argv[1]} == CHECK_FLAG) {
    std::cout << "cms-api: configuration is valid for " << config->listen_address << ':'
              << config->listen_port << " (tls " << config->tls_mode << ")\n";
    return 0;
  }

  const auto client =
      drogon::orm::DbClient::newPgClient(config->database_url, DATABASE_CONNECTIONS);
  auto database = cms::db::connection{std::make_shared<const cms::db::connection_config>(
      cms::db::connection_config{.client = client})};

  const auto snapshot = cms::api::load_settings(database);
  if (!snapshot) {
    return fail(snapshot.error().message);
  }

  auto store = std::make_shared<cms::SettingsStore>(*snapshot);
  register_routes(store);

  auto watcher = cms::api::SettingsWatcher{database, store, config->database_url,
                                           SETTINGS_POLL_SECONDS};
  watcher.start();

  auto& app = drogon::app();
  app.addListener(config->listen_address,
                  static_cast<std::uint16_t>(std::stoi(config->listen_port)));
  app.run();
  return 0;
}
