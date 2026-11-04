#include <cms/api/page_routes.hpp>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <drogon/drogon.h>

#include <cms/api/task_view.hpp>
#include <cms/api/web.hpp>
#include <cms/bank/curriculum.hpp>

namespace cms::api {
namespace {

auto html_response(std::string body) -> drogon::HttpResponsePtr {
  auto response = drogon::HttpResponse::newHttpResponse();
  response->setContentTypeString("text/html; charset=utf-8");
  response->setBody(std::move(body));
  return response;
}

auto text_response(std::string body, drogon::HttpStatusCode status) -> drogon::HttpResponsePtr {
  auto response = drogon::HttpResponse::newHttpResponse();
  response->setStatusCode(status);
  response->setContentTypeString("text/plain; charset=utf-8");
  response->setBody(std::move(body));
  return response;
}

/// The installation's own name, as the settings row publishes it. Empty when the row is absent, because a
/// heading with no name is still a page.
auto site_name_of(const SettingsStore& store) -> std::string {
  const auto settings = store.load();
  return settings == nullptr ? std::string{}
                             : setting_value<std::string>(*settings, "site.name").value_or("");
}

/// The one body an unknown, draft and hidden task all get back. A caller cannot tell which it hit, so the
/// route is not an oracle for the ids of tasks it may not see.
constexpr std::string_view NO_TASK = "no such task\n";

}  // namespace

auto register_home_route(const std::shared_ptr<SettingsStore>& store,
                         const std::shared_ptr<const db::connection_config>& database_config,
                         std::string_view template_root) -> void {
  drogon::app().registerHandler(
      "/",
      [store, database_config, template_root = std::string{template_root}](
          const drogon::HttpRequestPtr& request,
          std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto database = db::connection{database_config};
        const auto curriculum = bank::read_curriculum(database);
        if (!curriculum) {
          callback(text_response(curriculum.error().message, drogon::k500InternalServerError));
          return;
        }

        const auto locale = choose_locale(request->getParameter("lang"),
                                          request->getHeader("accept-language"));
        const auto page = render_page(template_root, "home.html",
                                      home_model(locale, *curriculum, site_name_of(*store)));
        if (!page) {
          callback(text_response(page.error().message, drogon::k500InternalServerError));
          return;
        }

        callback(html_response(*page));
      },
      {drogon::Get});
}

auto register_task_route(const std::shared_ptr<SettingsStore>& store,
                         const std::shared_ptr<const db::connection_config>& database_config,
                         std::string_view template_root) -> void {
  drogon::app().registerHandler(
      "/tasks/{}",
      [store, database_config, template_root = std::string{template_root}](
          const drogon::HttpRequestPtr& request,
          std::function<void(const drogon::HttpResponsePtr&)>&& callback,
          std::string_view task_id) {
        auto database = db::connection{database_config};
        const auto audience = audience_of_session(database, request->getCookie("cms_session"));
        if (!audience) {
          callback(text_response(audience.error().message, drogon::k500InternalServerError));
          return;
        }
        const auto task = read_task_page(database, task_id, *audience);
        if (!task) {
          callback(text_response(task.error().message, drogon::k500InternalServerError));
          return;
        }
        if (!*task) {
          callback(text_response(std::string{NO_TASK}, drogon::k404NotFound));
          return;
        }

        const auto locale = choose_locale(request->getParameter("lang"),
                                          request->getHeader("accept-language"));
        const auto page = render_page(template_root, "task.html",
                                      task_model(locale, **task, site_name_of(*store)));
        if (!page) {
          callback(text_response(page.error().message, drogon::k500InternalServerError));
          return;
        }

        callback(html_response(*page));
      },
      {drogon::Get});
}

}  // namespace cms::api
