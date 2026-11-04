#include <cms/api/verdict_routes.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include <drogon/drogon.h>
#include <sqlpp23/sqlpp23.h>

#include <cms/api/authorize.hpp>
#include <cms/api/http.hpp>
#include <cms/api/route_text.hpp>
#include <cms/api/task_view.hpp>
#include <cms/api/verdict_view.hpp>
#include <cms/api/web.hpp>
#include <cms/capability.hpp>
#include <cms/db/tables.hpp>

namespace cms::api {
namespace {

constexpr std::string_view SIGN_IN_PATH = "/signin";

/// The newest submission the viewer made for this task version, or "nothing yet" when there is none or it is
/// still unjudged. One read: a verdict write is atomic, so a row either has one and its time or neither.
auto latest_verdict(db::connection& database, std::string_view account_id,
                    std::string_view task_id, std::int32_t version) -> Result<VerdictView> {
  try {
    for (const auto& row :
         database(sqlpp::select(db::submissions.verdict)
                      .from(db::submissions)
                      .where(db::submissions.accountId == std::string{account_id} &&
                             db::submissions.taskId == std::string{task_id} &&
                             db::submissions.taskVersion == static_cast<std::int64_t>(version))
                      .order_by(db::submissions.submittedAt.desc())
                      .limit(1))) {
      auto view = VerdictView{};
      if (row.verdict.has_value()) {
        view.is_pending = false;
        // Copied out of the row: the connector's buffers do not outlive the loop.
        view.verdict = std::string{*row.verdict};
      }
      return view;
    }
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }
  return VerdictView{};
}

}  // namespace

auto register_verdict_route(const std::shared_ptr<const db::connection_config>& database_config)
    -> void {
  drogon::app().registerHandler(
      "/tasks/{}/verdict",
      [database_config](const drogon::HttpRequestPtr& request,
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
        const auto version = servable_version(database, task_id, viewer->audience, permitted.has_value());
        if (!version) {
          callback(text_response(std::string{INTERNAL_ERROR_BODY},
                                 drogon::k500InternalServerError));
          return;
        }
        if (!*version) {
          callback(text_response(std::string{NO_TASK_BODY}, drogon::k404NotFound));
          return;
        }
        const auto verdict = latest_verdict(database, viewer->principal_id, task_id, **version);
        if (!verdict) {
          callback(text_response(std::string{INTERNAL_ERROR_BODY},
                                 drogon::k500InternalServerError));
          return;
        }

        const auto locale = choose_locale(request->getParameter("lang"),
                                          request->getHeader("accept-language"));
        const auto fragment = render_fragment(
            verdict_fragment_source(verdict->is_pending),
            verdict_fragment_model(locale, task_id, *verdict));
        if (!fragment) {
          callback(text_response(std::string{INTERNAL_ERROR_BODY},
                                 drogon::k500InternalServerError));
          return;
        }
        callback(html_response(*fragment));
      },
      {drogon::Get});
}

}  // namespace cms::api
