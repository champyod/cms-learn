#include <cms/api/web.hpp>

#include <algorithm>
#include <cctype>
#include <exception>
#include <string>
#include <string_view>
#include <vector>

#include <inja/inja.hpp>

namespace cms::api {
namespace {

constexpr std::string_view THAI = "th";
constexpr std::string_view ENGLISH = "en";

/// The navigation and footer words the shell needs in both languages. Kept here rather than in a template
/// so one call site decides what a missing word falls back to.
struct Words {
  std::string_view curriculum;
  std::string_view submissions;
  std::string_view settings;
  std::string_view sign_in;
  std::string_view notice;
  std::string_view heading;
  std::string_view lede;
  std::string_view band;
  std::string_view start;
  std::string_view empty;
};

constexpr Words THAI_WORDS{.curriculum = "หลักสูตร",
                           .submissions = "งานที่ส่ง",
                           .settings = "ตั้งค่า",
                           .sign_in = "เข้าสู่ระบบ",
                           .notice = "เนื้อหาภาษาไทยและอังกฤษอยู่ในหน้าเดียวกัน",
                           .heading = "หลักสูตร",
                           .lede = "เลือกเส้นทางและเริ่มจากขั้นที่ตรงกับคุณ",
                           .band = "ระดับความยาก",
                           .start = "เริ่มขั้นนี้",
                           .empty = "ยังไม่มีหลักสูตร"};

constexpr Words ENGLISH_WORDS{.curriculum = "Curriculum",
                              .submissions = "Submissions",
                              .settings = "Settings",
                              .sign_in = "Sign in",
                              .notice = "Thai and English share one page",
                              .heading = "Curriculum",
                              .lede = "Pick a route and start at the stage that fits you",
                              .band = "Difficulty",
                              .start = "Start this stage",
                              .empty = "No curriculum yet"};

auto lower(std::string_view text) -> std::string {
  auto result = std::string{};
  result.reserve(text.size());
  for (const auto character : text) {
    result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
  }
  return result;
}

auto words_for(const Locale& locale) -> const Words& {
  return locale.code == THAI ? THAI_WORDS : ENGLISH_WORDS;
}

}  // namespace

auto choose_locale(std::string_view query_language, std::string_view accept_language) -> Locale {
  auto requested = lower(query_language);
  if (requested.empty() && !accept_language.empty()) {
    // Only the first tag matters: "th-TH,en;q=0.8" wants Thai, and the rest is a fallback list.
    const auto comma = accept_language.find(',');
    requested = lower(accept_language.substr(0, comma));
  }
  if (requested.starts_with(ENGLISH)) {
    return Locale{.code = std::string{ENGLISH}, .other = std::string{THAI}};
  }
  return Locale{.code = std::string{THAI}, .other = std::string{ENGLISH}};
}

auto label(const Locale& locale, std::string_view english, std::string_view thai)
    -> std::string {
  const auto is_thai = locale.code == THAI;
  const auto preferred = is_thai ? thai : english;
  const auto fallback = is_thai ? english : thai;
  if (!preferred.empty()) {
    return std::string{preferred};
  }
  return std::string{fallback};
}

auto build_model(const Locale& locale, const std::vector<bank::Track>& tracks,
                 std::string_view site_name) -> nlohmann::json {
  const auto& words = words_for(locale);
  const auto other_words = words_for(Locale{.code = locale.other, .other = locale.code});

  auto data = nlohmann::json{};
  data["locale"] = locale.code;
  data["other_locale"] = locale.other;
  data["other_locale_label"] = std::string{other_words.heading};
  data["site_name"] = std::string{site_name};
  data["is_signed_in"] = false;
  data["heading"] = std::string{words.heading};
  data["lede"] = std::string{words.lede};
  data["band_label"] = std::string{words.band};
  data["start_label"] = std::string{words.start};
  data["empty_label"] = std::string{words.empty};
  data["nav"] = {{"curriculum", std::string{words.curriculum}},
                 {"submissions", std::string{words.submissions}},
                 {"settings", std::string{words.settings}},
                 {"sign_in", std::string{words.sign_in}}};
  data["footer"] = {{"notice", std::string{words.notice}}};

  data["tracks"] = nlohmann::json::array();
  for (const auto& track : tracks) {
    auto entry = nlohmann::json{};
    entry["id"] = track.id;
    entry["title"] = label(locale, track.title_en, track.title_th);
    entry["description"] = label(locale, track.description_en, track.description_th);
    entry["stages"] = nlohmann::json::array();
    for (const auto& stage : track.stages) {
      auto listed = nlohmann::json{};
      listed["id"] = stage.id;
      listed["title"] = label(locale, stage.title_en, stage.title_th);
      listed["difficulty_min"] = stage.difficulty_min;
      listed["difficulty_max"] = stage.difficulty_max;
      listed["tags"] = stage.tags;
      entry["stages"].push_back(std::move(listed));
    }
    data["tracks"].push_back(std::move(entry));
  }
  return data;
}

}  // namespace

auto render_home(const std::string& template_root, const Locale& locale,
                 const std::vector<bank::Track>& tracks, std::string_view site_name)
    -> Result<std::string> {
  const auto data = build_model(locale, tracks, site_name);
  try {
    auto environment = inja::Environment{};
    environment.set_html_autoescape(true);
    environment.set_search_included_templates_in_files(true);
    return environment.render(template_root + "/home.html", data);
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::internal, error.what()));
  }
}

}  // namespace cms::api
