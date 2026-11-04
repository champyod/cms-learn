#include <cms/api/auth_routes.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include <drogon/drogon.h>

#include <cms/api/accounts.hpp>
#include <cms/api/auth.hpp>
#include <cms/api/auth_view.hpp>
#include <cms/api/form_body.hpp>
#include <cms/api/http.hpp>
#include <cms/api/route_text.hpp>
#include <cms/api/settings_defaults.hpp>
#include <cms/api/settings_page.hpp>
#include <cms/api/web.hpp>

namespace cms::api {
namespace {

constexpr std::string_view SESSION_COOKIE = "cms_session";
constexpr std::string_view ENABLED_FLAG = "1";
constexpr std::string_view HTTPS = "https";
constexpr std::string_view PROXY_TLS_MODE = "proxy";
constexpr std::string_view FORWARDED_PROTO_HEADER = "x-forwarded-proto";
constexpr std::string_view SIGN_IN_PATH = "/signin";
constexpr std::string_view HOME_PATH = "/";
constexpr std::int64_t SECONDS_PER_HOUR = 60 * 60;

/// Secure follows the connection the request arrived on: a direct TLS socket says so itself, and behind the
/// proxy the forwarded protocol does. Without TLS the flag stays off, so a local development login still works.
auto is_secure_request(const drogon::HttpRequestPtr& request, std::string_view tls_mode) -> bool {
  if (request->isOnSecureConnection()) {
    return true;
  }
  return tls_mode == PROXY_TLS_MODE && request->getHeader(std::string{FORWARDED_PROTO_HEADER}) == HTTPS;
}

/// The flags the sign-in page understands. Only these three canned notices exist, so nothing from a query
/// string can become page copy.
auto requested_notice(const drogon::HttpRequestPtr& request) -> SigninNotice {
  const auto verified = request->getParameter("verified");
  if (verified == "active") {
    return SigninNotice::verified;
  }
  if (verified == "pending") {
    return SigninNotice::approval_pending;
  }
  if (request->getParameter("registered") == ENABLED_FLAG) {
    return SigninNotice::registered;
  }
  return SigninNotice::none;
}

auto render_signin(const std::string& template_root, const Locale& locale,
                   std::string_view site_name, std::string_view message,
                   drogon::HttpStatusCode status) -> drogon::HttpResponsePtr {
  const auto page = render_page(template_root, "signin.html",
                                signin_model(locale, site_name, message));
  if (!page) {
    return text_response(std::string{INTERNAL_ERROR_BODY}, drogon::k500InternalServerError);
  }
  return html_response(*page, status);
}

auto render_signup(const std::string& template_root, const Locale& locale,
                   std::string_view site_name, std::string_view message,
                   drogon::HttpStatusCode status) -> drogon::HttpResponsePtr {
  const auto page = render_page(template_root, "signup.html",
                                signup_model(locale, site_name, message));
  if (!page) {
    return text_response(std::string{INTERNAL_ERROR_BODY}, drogon::k500InternalServerError);
  }
  return html_response(*page, status);
}

auto session_cookie(std::string_view session_id, int max_age_seconds, bool is_secure)
    -> drogon::Cookie {
  auto cookie = drogon::Cookie{std::string{SESSION_COOKIE}, std::string{session_id}};
  cookie.setPath("/");
  cookie.setHttpOnly(true);
  cookie.setSameSite(drogon::Cookie::SameSite::kLax);
  cookie.setSecure(is_secure);
  if (max_age_seconds > 0) {
    cookie.setMaxAge(max_age_seconds);
  }
  return cookie;
}

/// An expired cookie, so the browser drops it even when the response that revokes the row never arrives.
auto expired_cookie(bool is_secure) -> drogon::Cookie {
  auto cookie = drogon::Cookie{std::string{SESSION_COOKIE}, ""};
  cookie.setPath("/");
  cookie.setHttpOnly(true);
  cookie.setSameSite(drogon::Cookie::SameSite::kLax);
  cookie.setSecure(is_secure);
  cookie.setMaxAge(0);
  cookie.setExpiresDate(trantor::Date{0});
  return cookie;
}

/// Session lifetime, from the same declared setting the rest of the service reads. A non-positive value is a
/// configuration fault and refuses the sign-in rather than creating a session that expires immediately.
auto session_seconds(const SettingsSnapshot& settings) -> std::int64_t {
  const auto hours = setting_int(settings, "identity.session_absolute_hours");
  return hours > 0 ? hours * SECONDS_PER_HOUR : 0;
}

}  // namespace

auto register_auth_routes(const std::shared_ptr<SettingsStore>& store,
                          const std::shared_ptr<const db::connection_config>& database_config,
                          std::string_view template_root, std::string_view tls_mode) -> void {
  auto& app = drogon::app();

  app.registerHandler(
      "/signin",
      [store, template_root = std::string{template_root}](
          const drogon::HttpRequestPtr& request,
          std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        const auto locale = choose_locale(request->getParameter("lang"),
                                          request->getHeader("accept-language"));
        callback(render_signin(template_root, locale, site_name_of(*store),
                               signin_notice(locale, requested_notice(request)), drogon::k200OK));
      },
      {drogon::Get});

  app.registerHandler(
      "/signin",
      [store, database_config, template_root = std::string{template_root},
       tls_mode = std::string{tls_mode}](
          const drogon::HttpRequestPtr& request,
          std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto database = db::connection{database_config};
        const auto locale = choose_locale(request->getParameter("lang"),
                                          request->getHeader("accept-language"));
        const auto principal = authenticate(database, form_field(request->body(), "email"),
                                            form_field(request->body(), "password"));
        if (!principal) {
          const auto status = principal.error().code == ErrorCode::permission_denied
                                  ? drogon::k403Forbidden
                                  : drogon::k500InternalServerError;
          callback(render_signin(template_root, locale, site_name_of(*store),
                                 principal.error().message, status));
          return;
        }

        const auto settings = store->load();
        const auto seconds = settings == nullptr ? std::int64_t{0} : session_seconds(*settings);
        if (seconds <= 0) {
          callback(text_response(std::string{INTERNAL_ERROR_BODY},
                                 drogon::k500InternalServerError));
          return;
        }
        const auto session = create_session(database, *principal, seconds);
        if (!session) {
          callback(render_signin(template_root, locale, site_name_of(*store),
                                 session.error().message, drogon::k403Forbidden));
          return;
        }

        auto response = redirect(HOME_PATH);
        response->addCookie(session_cookie(*session, static_cast<int>(seconds),
                                           is_secure_request(request, tls_mode)));
        callback(response);
      },
      {drogon::Post});

  app.registerHandler(
      "/signup",
      [store, template_root = std::string{template_root}](
          const drogon::HttpRequestPtr& request,
          std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        const auto locale = choose_locale(request->getParameter("lang"),
                                          request->getHeader("accept-language"));
        callback(render_signup(template_root, locale, site_name_of(*store), "", drogon::k200OK));
      },
      {drogon::Get});

  app.registerHandler(
      "/signup",
      [store, database_config, template_root = std::string{template_root}](
          const drogon::HttpRequestPtr& request,
          std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto database = db::connection{database_config};
        const auto locale = choose_locale(request->getParameter("lang"),
                                          request->getHeader("accept-language"));
        const auto settings = store->load();
        if (settings == nullptr) {
          callback(text_response(std::string{INTERNAL_ERROR_BODY},
                                 drogon::k500InternalServerError));
          return;
        }
        const auto password = form_field(request->body(), "password");
        const auto valid = validate_password(*settings, password);
        if (!valid) {
          callback(render_signup(template_root, locale, site_name_of(*store),
                                 valid.error().message, drogon::k400BadRequest));
          return;
        }
        const auto signup = sign_up(database, *settings, form_field(request->body(), "email"),
                                    form_field(request->body(), "handle"));
        if (!signup) {
          const auto status = signup.error().code == ErrorCode::permission_denied
                                  ? drogon::k403Forbidden
                                  : drogon::k400BadRequest;
          callback(render_signup(template_root, locale, site_name_of(*store),
                                 signup.error().message, status));
          return;
        }
        const auto stored = set_password(database, *settings, signup->principal_id, password);
        if (!stored) {
          callback(text_response(std::string{INTERNAL_ERROR_BODY},
                                 drogon::k500InternalServerError));
          return;
        }
        callback(redirect(std::string{SIGN_IN_PATH} + "?registered=" + std::string{ENABLED_FLAG}));
      },
      {drogon::Post});

  app.registerHandler(
      "/verify",
      [database_config, store](
          const drogon::HttpRequestPtr& request,
          std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        const auto token = request->getParameter("token");
        if (token.empty()) {
          callback(text_response("the verification link carries no token", drogon::k400BadRequest));
          return;
        }
        const auto settings = store->load();
        if (settings == nullptr) {
          callback(text_response(std::string{INTERNAL_ERROR_BODY},
                                 drogon::k500InternalServerError));
          return;
        }
        auto database = db::connection{database_config};
        const auto verified = verify_email(database, *settings, token);
        if (!verified) {
          callback(text_response(verified.error().message, drogon::k400BadRequest));
          return;
        }
        callback(redirect(std::string{SIGN_IN_PATH} + "?verified=" +
                          (*verified ? "active" : "pending")));
      },
      {drogon::Get});

  app.registerHandler(
      "/signout",
      [database_config, tls_mode = std::string{tls_mode}](
          const drogon::HttpRequestPtr& request,
          std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto database = db::connection{database_config};
        const auto session_id = request->getCookie(std::string{SESSION_COOKIE});
        const auto ended = end_session(database, session_id);
        if (!ended) {
          callback(text_response(std::string{INTERNAL_ERROR_BODY},
                                 drogon::k500InternalServerError));
          return;
        }
        auto response = redirect(HOME_PATH);
        response->addCookie(expired_cookie(is_secure_request(request, tls_mode)));
        callback(response);
      },
      {drogon::Post});
}

}  // namespace cms::api
