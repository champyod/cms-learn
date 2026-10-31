#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include <cms/settings_snapshot.hpp>

namespace cms::api {

/// @brief One setting this service reads, with the value it assumes when the row is absent.
///
/// The table holds what an operator changed; this holds what the service does by default. Keeping the
/// defaults in code is what makes an absent row a known value rather than a missing fact, and it gives
/// the settings gate something to compare against the catalogue.
struct SettingDefault {
  std::string_view key;
  std::string_view value;
};

inline constexpr std::array k_setting_defaults{
    SettingDefault{"site.registration_open", "false"},
    SettingDefault{"site.registration_domain_allowlist", ""},
    SettingDefault{"site.registration_requires_approval", "true"},
    SettingDefault{"mail.enabled", "false"},
    SettingDefault{"mail.host", ""},
    SettingDefault{"mail.port", "587"},
    SettingDefault{"mail.username", ""},
    SettingDefault{"mail.password", ""},
    SettingDefault{"mail.from_address", ""},
    SettingDefault{"mail.starttls", "true"},
};

/// @brief Reads a boolean setting, falling back to the declared default.
[[nodiscard]] bool setting_bool(const SettingsSnapshot& snapshot, std::string_view key);

/// @brief Reads a text setting, falling back to the declared default.
[[nodiscard]] std::string setting_text(const SettingsSnapshot& snapshot, std::string_view key);

/// @brief Reads an integer setting, falling back to the declared default.
[[nodiscard]] std::int64_t setting_int(const SettingsSnapshot& snapshot, std::string_view key);

}  // namespace cms::api
