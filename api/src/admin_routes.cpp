#include <cms/api/admin_routes.hpp>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <drogon/drogon.h>

#include <cms/api/actor.hpp>
#include <cms/api/authorize.hpp>
#include <cms/api/csrf.hpp>
#include <cms/api/form_body.hpp>
#include <cms/api/http.hpp>
#include <cms/api/route_text.hpp>
#include <cms/api/settings_page.hpp>
#include <cms/api/settings_route.hpp>
#include <cms/api/web.hpp>
#include <cms/capability.hpp>
#include <cms/db/connection.hpp>
#include <cms/settings_snapshot.hpp>

namespace cms::api {
namespace {

constexpr std::string_view SESSION_COOKIE = "cms_session";
constexpr std::string_view SIGN_IN_PATH = "/signin";
constexpr std::string_view HOME_PATH = "/admin";
constexpr std::string_view ADMIN_KIND = "admin";
constexpr std::string_view CSRF_FIELD = "csrf_token";
constexpr std::string_view DONE_PARAMETER = "done";
constexpr std::string_view NO_PAGE_BODY = "no such page\n";
constexpr std::string_view ADMIN_ONLY_BODY = "the panel needs an administrator session\n";
constexpr std::string_view CSRF_REFUSED_BODY =
    "the form token is missing or does not belong to this session\n";

using Callback = std::function<void(const drogon::HttpResponsePtr&)>;

/// What every handler reads, built once when the routes are registered.
struct Panel {
  std::shared_ptr<const db::connection_config> database_config;
  std::shared_ptr<SettingsStore> store;
  std::shared_ptr<CsrfTokens> tokens;
  std::string template_root;
  std::uint16_t panel_port{0};
};

auto status_of(ErrorCode code) -> drogon::HttpStatusCode {
  switch (code) {
    case ErrorCode::invalid_argument:
      return drogon::k400BadRequest;
    case ErrorCode::not_found:
      return drogon::k404NotFound;
    case ErrorCode::permission_denied:
      return drogon::k403Forbidden;
    case ErrorCode::conflict:
      return drogon::k409Conflict;
    case ErrorCode::unavailable:
    case ErrorCode::internal:
      return drogon::k500InternalServerError;
  }
  return drogon::k500InternalServerError;
}

auto done_code(PanelAction action) -> std::string_view {
  switch (action) {
    case PanelAction::create_learner:
      return "learner-created";
    case PanelAction::create_admin:
      return "admin-created";
    case PanelAction::approve:
      return "learner-approved";
    case PanelAction::revoke_admin:
      return "admin-revoked";
    case PanelAction::issue_verification:
      return "verification-issued";
    case PanelAction::grant_role:
      return "role-granted";
  }
  return {};
}

/// The panel answers only the listener it was mounted on. Drogon keeps one route table for every
/// listener, so the port the request arrived on is what separates the panel from the public site.
auto is_panel_listener(const drogon::HttpRequestPtr& request, std::uint16_t panel_port) -> bool {
  return request->localAddr().toPort() == panel_port;
}

auto render_home(const std::shared_ptr<Panel>& panel, db::connection& database,
                 const drogon::HttpRequestPtr& request, const Account& account,
                 std::string_view notice, std::string_view message, drogon::HttpStatusCode status)
    -> drogon::HttpResponsePtr {
  const auto learners = read_learner_roster(database);
  if (!learners) {
    return text_response(learners.error().message, drogon::k500InternalServerError);
  }
  const auto admin_handle = read_admin_handle(database, account.id);
  if (!admin_handle) {
    return text_response(admin_handle.error().message, drogon::k500InternalServerError);
  }
  const auto session_id = request->getCookie(std::string{SESSION_COOKIE});
  const auto roles = read_roles(database);
  if (!roles) {
    return text_response(roles.error().message, drogon::k500InternalServerError);
  }
  const auto administrators = read_administrators(database);
  if (!administrators) {
    return text_response(administrators.error().message, drogon::k500InternalServerError);
  }
  const auto home = PanelHome{.admin_handle = *admin_handle,
                              .csrf_token = panel->tokens->token_for(session_id),
                              .notice = std::string{notice},
                              .message = std::string{message},
                              .learners = *learners,
                              .roles = *roles,
                              .administrators = *administrators};
  const auto locale = choose_locale(request->getParameter("lang"),
                                    request->getHeader("accept-language"));
  const auto page = render_page(panel->template_root, "admin_home.html",
                                panel_model(locale, site_name_of(*panel->store), home));
  if (!page) {
    return text_response(std::string{INTERNAL_ERROR_BODY}, drogon::k500InternalServerError);
  }
  return html_response(*page, status);
}

auto handle_home(const std::shared_ptr<Panel>& panel, const drogon::HttpRequestPtr& request,
                 Callback&& callback) -> void {
  if (!is_panel_listener(request, panel->panel_port)) {
    callback(text_response(std::string{NO_PAGE_BODY}, drogon::k404NotFound));
    return;
  }
  auto database = db::connection{panel->database_config};
  const auto account = account_of_session(database, request->getCookie(std::string{SESSION_COOKIE}));
  if (!account) {
    callback(text_response(account.error().message, drogon::k500InternalServerError));
    return;
  }
  if (account->empty()) {
    callback(redirect(SIGN_IN_PATH));
    return;
  }
  if (account->kind != ADMIN_KIND) {
    callback(text_response(std::string{ADMIN_ONLY_BODY}, drogon::k403Forbidden));
    return;
  }
  const auto permitted = authorize(database, account->kind, account->id, Capability::user_read);
  if (!permitted) {
    callback(text_response(permitted.error().message, status_of(permitted.error().code)));
    return;
  }
  callback(render_home(panel, database, request, *account,
                       request->getParameter(std::string{DONE_PARAMETER}), {}, drogon::k200OK));
}

auto handle_post(const std::shared_ptr<Panel>& panel, const drogon::HttpRequestPtr& request,
                 Callback&& callback, PanelAction action) -> void {
  if (!is_panel_listener(request, panel->panel_port)) {
    callback(text_response(std::string{NO_PAGE_BODY}, drogon::k404NotFound));
    return;
  }
  auto database = db::connection{panel->database_config};
  const auto session_id = request->getCookie(std::string{SESSION_COOKIE});
  const auto account = account_of_session(database, session_id);
  if (!account) {
    callback(text_response(account.error().message, drogon::k500InternalServerError));
    return;
  }
  if (account->empty() || account->kind != ADMIN_KIND) {
    callback(text_response(std::string{ADMIN_ONLY_BODY}, drogon::k403Forbidden));
    return;
  }
  if (!panel->tokens->is_token_for(session_id, form_field(request->body(), CSRF_FIELD))) {
    callback(text_response(std::string{CSRF_REFUSED_BODY}, drogon::k403Forbidden));
    return;
  }
  const auto settings = panel->store->load();
  if (settings == nullptr) {
    callback(text_response(std::string{INTERNAL_ERROR_BODY}, drogon::k500InternalServerError));
    return;
  }
  const auto actor = Actor{.kind = account->kind, .id = account->id};
  const auto outcome = perform_panel_action(database, *settings, actor, action, request->body());
  if (!outcome) {
    callback(render_home(panel, database, request, *account, {}, outcome.error().message,
                         status_of(outcome.error().code)));
    return;
  }
  callback(redirect(std::string{HOME_PATH} + "?done=" + std::string{done_code(action)}));
}

}  // namespace

auto register_admin_routes(const std::shared_ptr<const db::connection_config>& database_config,
                           const std::shared_ptr<SettingsStore>& store,
                           std::string_view template_root, std::uint16_t panel_port) -> void {
  auto panel = std::make_shared<Panel>(Panel{.database_config = database_config,
                                             .store = store,
                                             .tokens = std::make_shared<CsrfTokens>(),
                                             .template_root = std::string{template_root},
                                             .panel_port = panel_port});
  auto& app = drogon::app();
  app.registerHandler(
      std::string{HOME_PATH},
      [panel](const drogon::HttpRequestPtr& request, Callback&& callback) {
        handle_home(panel, request, std::move(callback));
      },
      {drogon::Get});

  const auto post = [&app, panel](std::string_view path, PanelAction action) {
    app.registerHandler(
        std::string{path},
        [panel, action](const drogon::HttpRequestPtr& request, Callback&& callback) {
          handle_post(panel, request, std::move(callback), action);
        },
        {drogon::Post});
  };
  post("/admin/create-learner", PanelAction::create_learner);
  post("/admin/create-admin", PanelAction::create_admin);
  post("/admin/approve", PanelAction::approve);
  post("/admin/revoke-admin", PanelAction::revoke_admin);
  post("/admin/issue-verification", PanelAction::issue_verification);
  post("/admin/grant-role", PanelAction::grant_role);
}

}  // namespace cms::api
