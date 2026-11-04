#include <cms/api/page_routes.hpp>

#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include <drogon/drogon.h>

#include <cms/api/authorize.hpp>
#include <cms/api/http.hpp>
#include <cms/api/route_text.hpp>
#include <cms/api/settings_page.hpp>
#include <cms/api/settings_route.hpp>
#include <cms/api/settings_view.hpp>
#include <cms/api/task_view.hpp>
#include <cms/api/web.hpp>
#include <cms/bank/curriculum.hpp>
#include <cms/capability.hpp>

namespace cms::api {
namespace {

constexpr std::string_view SIGN_IN_PATH = "/signin";

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
          callback(text_response(std::string{INTERNAL_ERROR_BODY},
                                 drogon::k500InternalServerError));
          return;
        }

        const auto locale = choose_locale(request->getParameter("lang"),
                                          request->getHeader("accept-language"));
        const auto page = render_page(template_root, "home.html",
                                      home_model(locale, *curriculum, site_name_of(*store)));
        if (!page) {
          callback(text_response(std::string{INTERNAL_ERROR_BODY},
                                 drogon::k500InternalServerError));
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
        const auto viewer = task_viewer(database, request->getCookie("cms_session"));
        if (!viewer) {
          if (viewer.error().code == ErrorCode::permission_denied) {
            callback(redirect(SIGN_IN_PATH));
          } else {
            callback(text_response(std::string{INTERNAL_ERROR_BODY},
                                   drogon::k500InternalServerError));
          }
          return;
        }
        const auto permitted = authorize(database, viewer->principal_id, Capability::content_read);
        if (!permitted && permitted.error().code != ErrorCode::permission_denied) {
          callback(text_response(std::string{INTERNAL_ERROR_BODY},
                                 drogon::k500InternalServerError));
          return;
        }
        const auto task =
            read_task_page(database, task_id, viewer->audience, permitted.has_value());
        if (!task) {
          callback(text_response(std::string{INTERNAL_ERROR_BODY},
                                 drogon::k500InternalServerError));
          return;
        }
        if (!*task) {
          callback(text_response(std::string{NO_TASK_BODY}, drogon::k404NotFound));
          return;
        }

        const auto locale = choose_locale(request->getParameter("lang"),
                                          request->getHeader("accept-language"));
        const auto page = render_page(template_root, "task.html",
                                      task_model(locale, **task, site_name_of(*store)));
        if (!page) {
          callback(text_response(std::string{INTERNAL_ERROR_BODY},
                                 drogon::k500InternalServerError));
          return;
        }

        callback(html_response(*page));
      },
      {drogon::Get});
}

/// Serves the settings page. An unauthenticated request is redirected to the sign-in page because it is not
/// forbidden, it is unauthenticated; a session without `settings_read` is forbidden. The values come from the
/// cached visible list, so a secret is redacted before this route ever sees it.
auto register_settings_route(const std::shared_ptr<SettingsStore>& store,
                             const std::shared_ptr<const db::connection_config>& database_config,
                             const std::shared_ptr<const SettingsView>& view,
                             std::string_view template_root) -> void {
  drogon::app().registerHandler(
      "/settings",
      [store, database_config, view, template_root = std::string{template_root}](
          const drogon::HttpRequestPtr& request,
          std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto database = db::connection{database_config};
        const auto principal = principal_of_session(database, request->getCookie("cms_session"));
        if (!principal) {
          callback(text_response(std::string{INTERNAL_ERROR_BODY},
                                 drogon::k500InternalServerError));
          return;
        }
        if (principal->empty()) {
          callback(redirect(SIGN_IN_PATH));
          return;
        }
        const auto permitted = authorize(database, *principal, Capability::settings_read);
        if (!permitted) {
          if (permitted.error().code != ErrorCode::permission_denied) {
            callback(text_response(std::string{INTERNAL_ERROR_BODY},
                                   drogon::k500InternalServerError));
          } else {
            callback(text_response(permitted.error().message, drogon::k403Forbidden));
          }
          return;
        }

        const auto snapshot = store->load();
        if (snapshot == nullptr) {
          callback(text_response(std::string{INTERNAL_ERROR_BODY},
                                 drogon::k500InternalServerError));
          return;
        }
        const auto locale = choose_locale(request->getParameter("lang"),
                                          request->getHeader("accept-language"));
        const auto page = render_page(
            template_root, "settings.html",
            settings_model(locale, site_name_of(*store), *view->for_snapshot(*snapshot),
                           request->getParameter("group")));
        if (!page) {
          callback(text_response(std::string{INTERNAL_ERROR_BODY},
                                 drogon::k500InternalServerError));
          return;
        }

        callback(html_response(*page));
      },
      {drogon::Get});
}

}  // namespace cms::api
