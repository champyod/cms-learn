#include <cms/api/settings_view.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <simdjson.h>

namespace cms::api {
namespace {

constexpr std::string_view SECRET_TIER = "secret";

auto field(const simdjson::dom::object& object, std::string_view name) -> std::string {
  auto view = std::string_view{};
  if (object.at_key(name).get_string().get(view) != simdjson::SUCCESS) {
    return {};
  }
  return std::string{view};
}

}  // namespace

auto load_catalogue(const std::filesystem::path& path) -> Result<std::vector<CatalogueEntry>> {
  auto stream = std::ifstream{path};
  if (!stream) {
    return std::unexpected(Error::make(ErrorCode::not_found,
                                       "cannot read the settings catalogue at " + path.string()));
  }
  auto buffer = std::ostringstream{};
  buffer << stream.rdbuf();
  const auto text = buffer.str();

  const auto json = simdjson::padded_string{text};
  auto parser = simdjson::dom::parser{};
  auto document = simdjson::dom::element{};
  if (parser.parse(json).get(document) != simdjson::SUCCESS) {
    return std::unexpected(
        Error::make(ErrorCode::invalid_argument, path.string() + ": not valid JSON"));
  }

  auto entries = std::vector<CatalogueEntry>{};
  auto array = simdjson::dom::array{};
  if (document.at_key("settings").get_array().get(array) != simdjson::SUCCESS) {
    return std::unexpected(
        Error::make(ErrorCode::invalid_argument, path.string() + ": settings is missing"));
  }
  for (const auto element : array) {
    auto object = simdjson::dom::object{};
    if (element.get_object().get(object) != simdjson::SUCCESS) {
      continue;
    }
    auto entry = CatalogueEntry{};
    entry.key = field(object, "key");
    entry.kind = field(object, "kind");
    entry.tier = field(object, "tier");
    entry.fallback = field(object, "default");
    if (!entry.key.empty()) {
      entries.push_back(std::move(entry));
    }
  }
  return entries;
}

auto visible_settings(const std::vector<CatalogueEntry>& catalogue, const SettingsSnapshot& snapshot)
    -> std::vector<VisibleSetting> {
  auto visible = std::vector<VisibleSetting>{};
  visible.reserve(catalogue.size());

  for (const auto& entry : catalogue) {
    auto shown = VisibleSetting{};
    shown.key = entry.key;
    shown.kind = entry.kind;
    shown.tier = entry.tier;
    if (entry.tier == SECRET_TIER) {
      // The value is not read at all. Redacting after reading would leave the plaintext in this frame for
      // the rest of the call, and one early return away from being logged.
      shown.value = std::string{REDACTED};
    } else {
      shown.value = entry.fallback;
      if (const auto* stored = snapshot.find(entry.key)) {
        if (const auto* text = std::get_if<std::string>(stored)) {
          shown.value = *text;
        } else if (const auto* flag = std::get_if<bool>(stored)) {
          shown.value = *flag ? "true" : "false";
        } else if (const auto* number = std::get_if<std::int64_t>(stored)) {
          shown.value = std::to_string(*number);
        }
      }
    }
    visible.push_back(std::move(shown));
  }
  return visible;
}

auto is_catalogued(const std::vector<CatalogueEntry>& catalogue, std::string_view key) -> bool {
  return std::ranges::any_of(catalogue,
                             [key](const CatalogueEntry& entry) { return entry.key == key; });
}

}  // namespace cms::api
