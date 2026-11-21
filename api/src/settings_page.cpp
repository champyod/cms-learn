#include <cms/api/settings_page.hpp>

#include <cms/api/settings_catalogue.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace cms::api {
namespace {

constexpr std::string_view SECRET_TIER = "secret";
/// What a stored secret renders as: the page says it is there and that it is sealed, never what it holds.
constexpr std::string_view SEALED_MARK = "sealed";
/// The marker the visible settings carry instead of a secret's value (settings_view.cpp). The page never sees the seal
/// itself: a secret is redacted before the route reads it, which is why the marker is what tells this page that a value
/// exists at all.
constexpr std::string_view REDACTED_MARK = "<redacted>";

struct SettingsWords {
  std::string_view title;
  std::string_view lede;
  std::string_view submit_label;
  std::string_view secret_hint;
};

constexpr SettingsWords THAI_SETTINGS{.title = "ตั้งค่า",
                                      .lede = "ค่าที่ระบบอ่านจริง แต่ละรายการมีชั้นการเข้าถึงกำกับ",
                                      .submit_label = "บันทึก",
                                      .secret_hint = "เก็บไว้แบบ sealed แล้ว ปล่อยว่างไว้เพื่อใช้ค่าเดิม"};

constexpr SettingsWords ENGLISH_SETTINGS{.title = "Settings",
                                         .lede = "The values the system reads, each with its access tier.",
                                         .submit_label = "Save",
                                         .secret_hint = "Stored sealed; leave it empty to keep the current value"};

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
    if (!is_implemented(setting.key)) {
      continue;
    }
    const auto group = group_of(setting.key);
    if (std::find(groups.begin(), groups.end(), group) == groups.end()) {
      groups.push_back(group);
    }
  }
  const auto requested = std::string{requested_group};
  const auto active = std::find(groups.begin(), groups.end(), requested) != groups.end()
                          ? requested
                          : (groups.empty() ? std::string{} : groups.front());

  auto data = shell_model(locale, site_name, LevelView{});
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

  // Only a setting a service reads is offered (ADR 0054 §5): the catalogue lists 109 and the platform reads 17, and a
  // field whose edit changes nothing is a promise the platform cannot keep. The unread ones are recorded in the record, not
  // shown with a warning: a warning still offers them.
  auto offered = std::vector<VisibleSetting>{};
  for (const auto& setting : settings) {
    if (is_implemented(setting.key)) {
      offered.push_back(setting);
    }
  }

  data["counts"] = {{"catalogued", static_cast<int>(settings.size())},
                    {"offered", static_cast<int>(offered.size())}};
  data["unread_note"] = pick(locale,
                             "Only the settings the system reads are offered. The rest are catalogued and unread, and are "
                             "recorded rather than shown.",
                             "แสดงเฉพาะค่าที่ระบบอ่านจริง ค่าที่เหลืออยู่ในสารบัญแต่ยังไม่มีใครอ่าน จึงบันทึกไว้แทนการแสดง");

  data["settings"] = nlohmann::json::array();
  for (const auto& setting : offered) {
    if (group_of(setting.key) != active) {
      continue;
    }
    const auto is_secret = setting.tier == SECRET_TIER;
    // A secret is write-only on a page: it shows that a value is stored and that it is sealed, and the value itself never
    // leaves the row (ADR 0054 §4).
    const auto shown = is_secret ? (setting.value == REDACTED_MARK ? std::string{SEALED_MARK} : std::string{})
                                 : setting.value;
    const auto reload = setting.reload;
    data["settings"].push_back({{"key", setting.key},
                                {"tier", setting.tier},
                                {"description", setting.key},
                                {"value", shown},
                                {"scope", setting_scope(setting.key)},
                                {"permission", setting_permission(setting.tier)},
                                {"effect", setting_effect(reload)},
                                {"sealed", is_secret}});
  }
  return data;
}

}  // namespace cms::api
