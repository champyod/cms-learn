#include <cms/api/web.hpp>

#include <cctype>
#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
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
  std::string_view limit_label;
  std::string_view example_label;
  std::string_view source_label;
  std::string_view submit_label;
  std::string_view verdict_pending;
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
                           .empty = "ยังไม่มีหลักสูตร",
                           .limit_label = "ขีดจำกัด",
                           .example_label = "ตัวอย่าง",
                           .source_label = "โค้ดของคุณ",
                           .submit_label = "ส่งคำตอบ",
                           .verdict_pending = "ยังไม่มีผลการตรวจ"};

constexpr Words ENGLISH_WORDS{.curriculum = "Curriculum",
                              .submissions = "Submissions",
                              .settings = "Settings",
                              .sign_in = "Sign in",
                              .notice = "Thai and English share one page",
                              .heading = "Curriculum",
                              .lede = "Pick a route and start at the stage that fits you",
                              .band = "Difficulty",
                              .start = "Start this stage",
                              .empty = "No curriculum yet",
                              .limit_label = "Limits",
                              .example_label = "Example",
                              .source_label = "Your code",
                              .submit_label = "Submit",
                              .verdict_pending = "No verdict yet"};

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

/// Component-wise, so `/srv/web` does not admit `/srv/webevil`, and a candidate that is the root itself is
/// not inside it: a directory is not an asset.
auto is_inside(const std::filesystem::path& root, const std::filesystem::path& candidate) -> bool {
  auto root_part = root.begin();
  auto candidate_part = candidate.begin();
  for (; root_part != root.end(); ++root_part, ++candidate_part) {
    if (candidate_part == candidate.end() || *candidate_part != *root_part) {
      return false;
    }
  }
  return candidate_part != candidate.end();
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

/// The shell fields every page fills the same way: locale, the language switch, the site name, navigation
/// and footer. One function so a second page cannot present a different shell than the first.
auto shell_model(const Locale& locale, std::string_view site_name) -> nlohmann::json {
  const auto& words = words_for(locale);
  const auto other_words = words_for(Locale{.code = locale.other, .other = locale.code});

  auto data = nlohmann::json{};
  data["locale"] = locale.code;
  data["other_locale"] = locale.other;
  data["other_locale_label"] = std::string{other_words.heading};
  data["site_name"] = std::string{site_name};
  data["is_signed_in"] = false;
  data["nav"] = {{"curriculum", std::string{words.curriculum}},
                 {"submissions", std::string{words.submissions}},
                 {"settings", std::string{words.settings}},
                 {"sign_in", std::string{words.sign_in}}};
  data["footer"] = {{"notice", std::string{words.notice}}};
  return data;
}


auto home_model(const Locale& locale, const std::vector<bank::Track>& tracks,
                std::string_view site_name) -> nlohmann::json {
  const auto& words = words_for(locale);
  auto data = shell_model(locale, site_name);
  data["heading"] = std::string{words.heading};
  data["lede"] = std::string{words.lede};
  data["band_label"] = std::string{words.band};
  data["start_label"] = std::string{words.start};
  data["empty_label"] = std::string{words.empty};

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

auto task_model(const Locale& locale, const TaskPage& task, std::string_view site_name)
    -> nlohmann::json {
  const auto& words = words_for(locale);
  auto data = shell_model(locale, site_name);
  data["example_label"] = std::string{words.example_label};
  data["source_label"] = std::string{words.source_label};
  data["submit_label"] = std::string{words.submit_label};
  data["verdict_pending"] = std::string{words.verdict_pending};
  data["submit_action"] = "/tasks/" + task.id + "/submit";
  data["verdict_url"] = "/tasks/" + task.id + "/verdict";
  data["task"] = {{"title", label(locale, task.title_en, task.title_th)},
                  {"limit_label", std::string{words.limit_label}},
                  {"time_ms", task.time_limit_ms},
                  {"memory_mb", task.memory_limit_mb},
                  {"statement", label(locale, task.statement_en, task.statement_th)},
                  {"example_input", task.example_input},
                  {"example_output", task.example_output},
                  {"starter_source", ""}};
  return data;
}

auto render_page(const std::string& template_root, std::string_view page,
                 const nlohmann::json& model) -> Result<std::string> {
  try {
    auto environment = inja::Environment{};
    environment.set_html_autoescape(true);
    environment.set_search_included_templates_in_files(true);
    return environment.render_file(template_root + "/" + std::string{page}, model);
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::internal, error.what()));
  }
}

auto static_file(std::string_view static_root, std::string_view relative)
    -> std::optional<std::string> {
  if (relative.empty()) {
    return std::nullopt;
  }
  auto error = std::error_code{};
  const auto root = std::filesystem::weakly_canonical(std::filesystem::path{static_root}, error);
  if (error) {
    return std::nullopt;
  }
  const auto candidate =
      std::filesystem::weakly_canonical(root / std::filesystem::path{relative}, error);
  if (error || !is_inside(root, candidate)) {
    return std::nullopt;
  }
  if (!std::filesystem::is_regular_file(candidate, error) || error) {
    return std::nullopt;
  }
  return candidate.string();
}

}  // namespace cms::api
