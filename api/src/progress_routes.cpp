#include <cms/api/progress_routes.hpp>

#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include <drogon/drogon.h>

#include <cms/api/auth.hpp>
#include <cms/api/http.hpp>
#include <cms/api/progress_view.hpp>
#include <cms/api/route_text.hpp>
#include <cms/api/settings_page.hpp>
#include <cms/api/settings_route.hpp>
#include <cms/api/web.hpp>

namespace cms::api {
namespace {

constexpr std::string_view SIGN_IN_PATH = "/signin";
constexpr std::string_view LEARNER_KIND = "learner";

}  // namespace

auto register_progress_route(const std::shared_ptr<SettingsStore>& store,
                             const std::shared_ptr<const db::connection_config>& database_config,
                             std::string_view template_root) -> void {
  drogon::app().registerHandler(
      "/progress",
      [store, database_config, template_root = std::string{template_root}](
          const drogon::HttpRequestPtr& request,
          std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto database = db::connection{database_config};
        const auto account = account_of_session(database, request->getCookie("cms_session"));
        if (!account) {
          callback(text_response(std::string{INTERNAL_ERROR_BODY},
                                 drogon::k500InternalServerError));
          return;
        }
        if (account->empty()) {
          callback(redirect(SIGN_IN_PATH));
          return;
        }
        if (account->kind != LEARNER_KIND) {
          callback(text_response("progress is a learner's own page\n", drogon::k403Forbidden));
          return;
        }

        const auto view = read_progress(database, account->id);
        if (!view) {
          callback(text_response(std::string{INTERNAL_ERROR_BODY},
                                 drogon::k500InternalServerError));
          return;
        }
        const auto locale = choose_locale(request->getParameter("lang"),
                                          request->getHeader("accept-language"));
        const auto page = render_page(template_root, "progress.html",
                                      progress_model(locale, *view, site_name_of(*store)));
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
