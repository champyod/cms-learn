#include <cms/api/settings_defaults.hpp>

#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>

namespace cms::api {
namespace {

auto declared(std::string_view key) -> std::string_view {
  for (const auto& entry : k_setting_defaults) {
    if (entry.key == key) {
      return entry.value;
    }
  }
  return {};
}

auto raw_text(const SettingsSnapshot& snapshot, std::string_view key) -> std::string_view {
  if (const auto* value = snapshot.find(key); value != nullptr) {
    if (const auto* text = std::get_if<std::string>(value); text != nullptr) {
      return *text;
    }
  }
  return declared(key);
}

}  // namespace

auto setting_bool(const SettingsSnapshot& snapshot, std::string_view key) -> bool {
  if (const auto* value = snapshot.find(key); value != nullptr) {
    if (const auto* flag = std::get_if<bool>(value); flag != nullptr) {
      return *flag;
    }
  }
  return declared(key) == "true";
}

auto setting_text(const SettingsSnapshot& snapshot, std::string_view key) -> std::string {
  return std::string{raw_text(snapshot, key)};
}

auto setting_int(const SettingsSnapshot& snapshot, std::string_view key) -> std::int64_t {
  if (const auto* value = snapshot.find(key); value != nullptr) {
    if (const auto* number = std::get_if<std::int64_t>(value); number != nullptr) {
      return *number;
    }
  }
  const auto text = declared(key);
  auto parsed = std::int64_t{};
  const auto [stop, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
  return error == std::errc{} && stop == text.data() + text.size() ? parsed : 0;
}

}  // namespace cms::api
