#include <charconv>
#include <cstdint>
#include <flat_map>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>

#include <cms/api/settings_source.hpp>
#include <cms/db/settings_query.hpp>

namespace cms::api {
namespace {

auto parse_bool(std::string_view text, SettingValue& out) -> bool {
  if (text == "true") {
    out = true;
    return true;
  }
  if (text == "false") {
    out = false;
    return true;
  }
  return false;
}

auto parse_int(std::string_view text, SettingValue& out) -> bool {
  auto value = std::int64_t{};
  const auto* first = text.data();
  const auto* last = first + text.size();
  const auto [stop, error] = std::from_chars(first, last, value);
  if (error != std::errc{} || stop != last) {
    return false;
  }
  out = value;
  return true;
}

/// The `kind` column says what the text is; a row whose text does not match its kind is a schema a
/// reader cannot trust, so it is a failure rather than a silently dropped setting.
auto parse_value(std::string_view kind, std::string_view text, SettingValue& out) -> bool {
  if (kind == "bool") {
    return parse_bool(text, out);
  }
  if (kind == "int") {
    return parse_int(text, out);
  }
  if (kind == "text") {
    out = std::string{text};
    return true;
  }
  return false;
}

auto read_version(db::connection& database) -> Result<std::optional<std::int64_t>> {
  for (const auto& row : database(db::settings_version_query())) {
    return static_cast<std::int64_t>(row.version);
  }
  return std::optional<std::int64_t>{};
}

}  // namespace

auto load_settings(db::connection& database) -> Result<std::shared_ptr<const SettingsSnapshot>> {
  auto values = std::flat_map<std::string, SettingValue, std::less<>>{};
  for (const auto& row : database(db::settings_snapshot_query())) {
    const auto key = std::string{row.key};
    const auto kind = std::string_view{row.kind};
    const auto text = std::string_view{row.valueText};

    auto value = SettingValue{};
    if (!parse_value(kind, text, value)) {
      return std::unexpected(Error::make(
          ErrorCode::invalid_argument,
          "setting " + key + " does not hold a value of its declared kind " + std::string{kind}));
    }
    values.emplace(key, std::move(value));
  }

  const auto version = read_version(database);
  if (!version) {
    return std::unexpected(version.error());
  }
  // The trigger creates the counter row on the first settings write, so an empty database has none.
  // Rows without a counter are a different thing: something wrote settings without moving the version.
  if (!version->has_value() && !values.empty()) {
    return std::unexpected(Error::make(
        ErrorCode::unavailable,
        "settings_meta holds no row although settings does, so the version cannot be trusted"));
  }

  return std::make_shared<const SettingsSnapshot>(
      SettingsSnapshot{version->value_or(0), std::move(values)});
}

}  // namespace cms::api
