#include <cms/api/settings_page.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace cms::api {
namespace {

constexpr std::string_view SECRET_TIER = "secret";

struct SettingsWords {
  std::string_view title;
  std::string_view lede;
  std::string_view submit_label;
  std::string_view secret_hint;
};

constexpr SettingsWords THAI_SETTINGS{.title = "ตั้งค่า",
                                      .lede = "ค่าที่ระบบอ่านจริง แต่ละรายการมีชั้นการเข้าถึงกำกับ",
                                      .submit_label = "บันทึก",
                                      .secret_hint = "ปล่อยว่างไว้เพื่อคงค่าเดิม"};

constexpr SettingsWords ENGLISH_SETTINGS{.title = "Settings",
                                         .lede = "The values the system reads, each with its access tier.",
                                         .submit_label = "Save",
                                         .secret_hint = "Leave empty to keep the current value"};

auto group_of(std::string_view key) -> std::string {
  const auto separator = key.find('.');
  return std::string{key.substr(0, separator)};
}

auto pick(const Locale& locale, std::string_view english, std::string_view thai) -> std::string {
  return label(locale, english, thai);
}

}  // namespace

auto site_name_of(const SettingsStore& store) -> std::string {
  const auto settings = store.load();
  return settings == nullptr ? std::string{}
                             : setting_value<std::string>(*settings, "site.name").value_or("");
}

auto settings_model(const Locale& locale, std::string_view site_name,
                    const std::vector<VisibleSetting>& settings, std::string_view requested_group)
    -> nlohmann::json {
  auto groups = std::vector<std::string>{};
  for (const auto& setting : settings) {
    const auto group = group_of(setting.key);
    if (std::find(groups.begin(), groups.end(), group) == groups.end()) {
      groups.push_back(group);
    }
  }
  const auto requested = std::string{requested_group};
  const auto active = std::find(groups.begin(), groups.end(), requested) != groups.end()
                          ? requested
                          : (groups.empty() ? std::string{} : groups.front());

  auto data = shell_model(locale, site_name);
  data["title"] = pick(locale, ENGLISH_SETTINGS.title, THAI_SETTINGS.title);
  data["lede"] = pick(locale, ENGLISH_SETTINGS.lede, THAI_SETTINGS.lede);
  data["submit_label"] = pick(locale, ENGLISH_SETTINGS.submit_label, THAI_SETTINGS.submit_label);
  data["secret_hint"] = pick(locale, ENGLISH_SETTINGS.secret_hint, THAI_SETTINGS.secret_hint);
  data["action"] = "/settings";
  data["active_group"] = active;

  data["groups"] = nlohmann::json::array();
  for (const auto& group : groups) {
    data["groups"].push_back({{"key", group}});
  }

  data["settings"] = nlohmann::json::array();
  for (const auto& setting : settings) {
    if (group_of(setting.key) != active) {
      continue;
    }
    const auto is_secret = setting.tier == SECRET_TIER;
    data["settings"].push_back({{"key", setting.key},
                                {"tier", setting.tier},
                                {"description", setting.key},
                                {"value", is_secret ? std::string{} : setting.value}});
  }
  return data;
}

}  // namespace cms::api
