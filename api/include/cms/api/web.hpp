#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <cms/api/progress_view.hpp>
#include <cms/bank/bank.hpp>
#include <cms/error.hpp>

namespace cms::api {

/// @brief The two locales the site is authored in.
struct Locale {
  /// The locale the page is rendered in.
  std::string code{"th"};
  /// The locale the switch offers.
  std::string other{"en"};
};

/// @brief The locale a request wants.
///
/// @param query_language The `lang` query parameter, empty when absent.
/// @param accept_language The `Accept-Language` header, empty when absent.
/// @return Thai unless the request asks for English, because the site's readers are primarily Thai and a
/// page in the wrong language is worse than a page in the default one.
[[nodiscard]] Locale choose_locale(std::string_view query_language,
                                   std::string_view accept_language);

/// @brief The label to show for @p locale.
///
/// @return @p thai when the locale is Thai and that label exists, otherwise @p english when it exists,
/// otherwise @p thai. A missing translation falls back to the other locale rather than rendering an empty
/// heading: content is authored by people, and one absent field must not blank a page.
[[nodiscard]] std::string label(const Locale& locale, std::string_view english,
                                std::string_view thai);

/// @brief One rung of the level vocabulary: a named band over the one difficulty scale.
///
/// The meter in the shell and the filter on the all-tasks page read this one type, so a rung cannot be
/// named one way in the navigation and another in the list.
struct LevelRung {
  std::string id;
  std::string title_en;
  /// Empty until the content carries a Thai title; the model falls back to English.
  std::string title_th;
  std::int32_t difficulty_min{0};
  std::int32_t difficulty_max{0};
};

/// @brief The level vocabulary a page shows, and the rung this request's learner has chosen.
struct LevelView {
  std::vector<LevelRung> rungs;
  /// Empty when the request carries no learner, or the learner has chosen no rung.
  std::string current_id;
  /// True when the request carries a learner account: the meter is that learner's own control, and an
  /// administrator or a signed-out visitor has none.
  bool is_learner{false};
};

/// @brief The fields every page's shell reads: locale, the language switch, the site name, navigation and
/// footer.
///
/// @param locale The locale the page is rendered in.
/// @param site_name The installation's own name.
/// @param levels The rungs the shell's meter offers, and the learner's own rung.
/// @return The shared part of a page model, built once so a second page cannot present a different shell.
///
/// The meter is the shell's, not a page's: it sits immediately before the language switch on every page
/// that carries a learner, so no page can offer a different control.
[[nodiscard]] nlohmann::json shell_model(const Locale& locale, std::string_view site_name,
                                         const LevelView& levels);

/// @brief Builds the model `home.html` renders from.
///
/// @param locale The locale to render in.
/// @param tracks The curriculum to list.
/// @param site_name The installation's own name.
/// @return The model, with every word the locale cannot supply replaced by the other locale's.
[[nodiscard]] nlohmann::json home_model(const Locale& locale,
                                        const std::vector<bank::Track>& tracks,
                                        std::string_view site_name, const LevelView& levels);

/// @brief One task as its page reads it: metadata, both statement locales and the first example.
///
/// The reference solution is deliberately absent: the page never carries it, so no model builder can leak
/// what it was never given. A task with no statement or no example is an error, never a placeholder.
/// A statement travels as the text the author wrote, because the shipped template autoescapes its slot and
/// HTML handed to the model would reach the reader as visible tags.
struct TaskPage {
  std::string id;
  std::string title_en;
  std::string title_th;
  std::int32_t time_limit_ms{0};
  std::int32_t memory_used_limit_mb{0};
  std::int32_t memory_reserved_limit_mb{0};
  std::string statement_en;
  std::string statement_th;
  std::string example_input;
  std::string example_output;
};

/// @brief Builds the model `task.html` renders from.
///
/// @param locale The locale to render in.
/// @param task The task the route resolved and may serve.
/// @param site_name The installation's own name.
/// @return The model, with the same locale fallback the home page uses.
///
/// `starter_source` is always empty: the content model defines no starter file, and the field must never
/// fall back to the reference solution.
[[nodiscard]] nlohmann::json task_model(const Locale& locale, const TaskPage& task,
                                        std::string_view site_name, const LevelView& levels);

/// @brief One row of the all-tasks page: what a learner chooses a task by, and nothing more.
struct TaskListItem {
  std::string id;
  std::string title_en;
  std::string title_th;
  std::int32_t difficulty{0};
};

/// @brief What the all-tasks page shows: the narrowing the request asked for, and the rows that survived it.
///
/// The band travels as the two numbers the filter used, never as a level id: a level is a function of the
/// difficulty (ADR 0013 §3), so a stored id would be a second vocabulary that could disagree with it.
struct TaskListPage {
  /// Both zero when the request named no level band.
  std::int32_t level_min{0};
  std::int32_t level_max{0};
  /// Empty when the request named no stage; `stage_track` and `stage_id` travel together.
  std::string stage_track;
  std::string stage_id;
  std::string stage_title_en;
  std::string stage_title_th;
  /// The selected vocabulary ids, in the order the request named them.
  std::vector<std::string> topics;
  std::vector<TaskListItem> tasks;
};

/// @brief Builds the model `tasks.html` renders from.
///
/// @param locale The locale to render in.
/// @param page The filters the request applied and the tasks the filters left.
/// @param vocabulary Every topic the filter may offer, so a chip is named the same way everywhere.
/// @param site_name The installation's own name.
/// @return The model, with the same locale fallback the other pages use.
[[nodiscard]] nlohmann::json task_list_model(const Locale& locale, const TaskListPage& page,
                                             const std::vector<bank::Topic>& vocabulary,
                                             std::string_view site_name, const LevelView& levels);

/// @brief Builds the model `progress.html` renders from.
///
/// @param locale The locale to render in.
/// @param view What the learner's own `progress` rows say, already counted per topic.
/// @param site_name The installation's own name.
/// @return The model, with the same locale fallback every page uses.
///
/// A topic with no rows is marked untried rather than ranked: the schema holds no threshold, so this model
/// has no way to call a topic weak and does not pretend to.
[[nodiscard]] nlohmann::json progress_model(const Locale& locale, const ProgressView& view,
                                            std::string_view site_name);

/// @brief Renders one template of @p template_root against @p model.
///
/// @param template_root The directory holding the templates and their partials.
/// @param page The template file to render, relative to @p template_root.
/// @param model The values the template reads.
/// @return The HTML, or the reason it could not be produced.
///
/// The model is the caller's, so a page's data is decided where the page's data comes from rather
/// than by a renderer that would have to know every page.
[[nodiscard]] Result<std::string> render_page(const std::string& template_root,
                                              std::string_view page,
                                              const nlohmann::json& model);

/// @brief Renders @p source as an Inja fragment against @p model.
///
/// The htmx fragments are not pages: the design port generates one full document per page, so a fragment
/// that is a single element keeps its source beside its model. Autoescape is the same one `render_page`
/// sets, so a value interpolated here can never become markup.
[[nodiscard]] Result<std::string> render_fragment(std::string_view source,
                                                  const nlohmann::json& model);

/// @brief The phrase the task page and its verdict fragment both show while no verdict has arrived.
[[nodiscard]] std::string verdict_pending_label(const Locale& locale);

/// @brief The file a request for @p relative under @p static_root may serve.
///
/// @param static_root The directory the operator published assets in.
/// @param relative The path the request named, with no leading separator.
/// @return The path to serve, or nothing when it is outside @p static_root or is not a regular file.
///
/// The candidate is resolved before it is compared, so `..`, an absolute path and a symlink out of the
/// tree all land outside the root and are refused by the same check instead of by three string rules.
[[nodiscard]] std::optional<std::string> static_file(std::string_view static_root,
                                                     std::string_view relative);

}  // namespace cms::api
