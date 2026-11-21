#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace cms::api {

/// @brief The settings a service actually reads, which are the only ones the page offers (ADR 0054).
///
/// The catalogue in `deploy/catalogue/settings.json` describes 109 keys and the platform reads 17 of them: the rest are
/// declarations of intent, not settings, and offering one would promise an effect the platform does not have. The list is
/// hand-kept beside the code that reads each key, because a generated list would go stale exactly where it matters — the
/// reader is deleted and the key keeps being offered. `settings_route_test.sh` holds the page to it, in both directions.
inline const std::vector<std::string_view>& implemented_settings() {
  static const auto keys = std::vector<std::string_view>{
      "site.name",
      "site.registration_open",
      "site.registration_domain_allowlist",
      "site.registration_requires_approval",
      "site.registration_requires_email",
      "identity.session_absolute_hours",
      "identity.password_min_length",
      "identity.elevation_minutes",
      "identity.argon2_memory_kib",
      "identity.argon2_iterations",
      "mail.enabled",
      "mail.host",
      "mail.port",
      "mail.username",
      "mail.password",
      "mail.from_address",
      "mail.starttls",
  };
  return keys;
}

/// @param key A catalogue key.
/// @return Whether a service reads it, so the page may offer it.
inline auto is_implemented(std::string_view key) -> bool {
  for (const auto candidate : implemented_settings()) {
    if (candidate == key) {
      return true;
    }
  }
  return false;
}

/// @param key A catalogue key.
/// @return The part of the system the setting belongs to, which is the prefix the catalogue's groups already use.
inline auto setting_scope(std::string_view key) -> std::string {
  const auto separator = key.find('.');
  return std::string{key.substr(0, separator)};
}

/// @param tier The catalogue tier of the setting.
/// @return The capability an operator needs to change it. A secret is the one tier the route separates: writing it needs
/// `settings:secret_write` on top of `settings:write`, which is why the page says which one a field asks for.
inline auto setting_permission(std::string_view tier) -> std::string_view {
  return tier == "secret" ? "settings:secret_write" : "settings:write";
}

/// @brief What a change costs, in the operator's words, for the reload class the catalogue records.
///
/// @param reload `hot` or `restart`, as the catalogue declares it.
/// @return The sentence the page shows beside the field, before the change is made.
inline auto setting_effect(std::string_view reload) -> std::string_view {
  return reload == "hot" ? "takes effect immediately"
                         : "takes effect after the api service restarts; the running service keeps the old value until then";
}

}  // namespace cms::api
