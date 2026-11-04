#pragma once

#include <memory>
#include <string_view>

#include <cms/db/connection.hpp>
#include <cms/settings_snapshot.hpp>

namespace cms::api {

/// @brief Registers the sign-in, sign-up, verification and sign-out routes.
///
/// @param store The settings the registration policy and session lifetime are read from.
/// @param database_config The pool accounts and sessions are read and written through.
/// @param template_root The directory the pages are rendered from.
/// @param tls_mode The host's TLS mode; `proxy` makes a forwarded HTTPS request count as a secure request,
/// which is what the session cookie's `Secure` flag follows.
auto register_auth_routes(const std::shared_ptr<SettingsStore>& store,
                          const std::shared_ptr<const db::connection_config>& database_config,
                          std::string_view template_root, std::string_view tls_mode) -> void;

}  // namespace cms::api
