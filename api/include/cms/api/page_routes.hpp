#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <cms/api/settings_view.hpp>
#include <cms/api/web.hpp>
#include <cms/bank/bank.hpp>
#include <cms/db/connection.hpp>
#include <cms/settings_snapshot.hpp>

namespace cms::api {

/// @brief One stage's path state, as the route read it: what the learner has done, and what comes next.
///
/// `next_title` arrives already in the page's locale: the route knows the reader's language, the composer
/// does not have to.
struct StagePath {
  std::string track_id;
  std::string stage_id;
  std::int64_t done{0};
  std::int64_t total{0};
  std::string next_id;
  std::string next_title;
  bool has_next{false};
};

/// @brief The five sentences the path state adds to the shell's own vocabulary.
struct PathWords {
  std::string_view done;
  std::string_view next;
  std::string_view finished;
  std::string_view no_next;
  std::string_view empty_stage;
};

/// @brief The words this page adds, in the locale the reader asked for.
[[nodiscard]] inline auto path_words(std::string_view locale_code) -> PathWords {
  if (locale_code == "en") {
    return PathWords{.done = "done",
                     .next = "Next task",
                     .finished = "Nothing left in this stage",
                     .no_next = "Nothing new to offer in this stage right now",
                     .empty_stage = "This stage has no tasks yet"};
  }
  return PathWords{.done = "ทำแล้ว",
                   .next = "แบบฝึกหัดถัดไป",
                   .finished = "ขั้นนี้ทำครบแล้ว",
                   .no_next = "ยังไม่มีแบบฝึกหัดใหม่ในขั้นนี้ตอนนี้",
                   .empty_stage = "ขั้นนี้ยังไม่มีแบบฝึกหัด"};
}

/// @brief Builds the one model `home.html` renders.
///
/// @param locale The locale to render in.
/// @param tracks The curriculum, as `bank::read_curriculum` returns it.
/// @param paths What the learner has done per stage and the task the bank offers next; empty for a visitor.
/// @param site_name The installation's own name.
/// @param levels The rung vocabulary the shell's meter draws.
/// @param has_learner True when the request carries a learner — the only case that has path state at all.
/// @return The model, with the path words and every stage's fields set, or unset together.
///
/// The route and the test both call this, which is what keeps `home.html` from ever being rendered from a
/// model a second builder half-filled: there is one place that knows which keys the template reads.
[[nodiscard]] inline auto home_page_model(const Locale& locale, const std::vector<bank::Track>& tracks,
                                          const std::vector<StagePath>& paths,
                                          std::string_view site_name, const LevelView& levels,
                                          bool has_learner) -> nlohmann::json {
  auto model = home_model(locale, tracks, site_name, levels);
  const auto words = path_words(locale.code);
  model["done_label"] = std::string{words.done};
  model["next_label"] = std::string{words.next};
  model["finished_label"] = std::string{words.finished};
  model["no_next_label"] = std::string{words.no_next};
  model["empty_stage_label"] = std::string{words.empty_stage};
  model["has_learner"] = has_learner;

  const auto path_for = [&paths](std::string_view track_id,
                                 std::string_view stage_id) -> const StagePath* {
    for (const auto& path : paths) {
      if (path.track_id == track_id && path.stage_id == stage_id) {
        return &path;
      }
    }
    return nullptr;
  };

  for (auto& track_json : model["tracks"]) {
    const auto track_id = track_json.value("id", std::string{});
    for (auto& stage_json : track_json["stages"]) {
      const auto stage_id = stage_json.value("id", std::string{});
      const auto* path = path_for(track_id, stage_id);
      const auto done = path == nullptr ? std::int64_t{0} : path->done;
      const auto total = path == nullptr ? std::int64_t{0} : path->total;
      stage_json["done"] = done;
      stage_json["total"] = total;
      stage_json["is_empty"] = total == 0;
      stage_json["has_next"] = path != nullptr && path->has_next;
      stage_json["is_finished"] = total > 0 && done >= total;
      stage_json["next_id"] = path == nullptr ? std::string{} : path->next_id;
      stage_json["next_title"] = path == nullptr ? std::string{} : path->next_title;
    }
  }
  return model;
}

/// @brief Registers `GET /`, the curriculum page.
///
/// @param store The settings the page shell reads its site name from.
/// @param database_config The pool the curriculum is read through.
/// @param template_root The directory the template is rendered from.
///
/// The curriculum is read per request, so a route imported into the database appears without a restart and
/// an unreachable database is reported rather than served as a stale page.
auto register_home_route(const std::shared_ptr<SettingsStore>& store,
                         const std::shared_ptr<const db::connection_config>& database_config,
                         std::string_view template_root) -> void;

/// @brief Registers `GET /tasks/{id}`, the task page.
///
/// @param store The settings the page shell reads its site name from.
/// @param database_config The pool the task is read through.
/// @param template_root The directory the template is rendered from.
///
/// The task is read through the published-and-visible predicate, and an unknown, a draft and a hidden task
/// all answer 404 with the same body, so the route cannot be used to enumerate hidden ids.
auto register_task_route(const std::shared_ptr<SettingsStore>& store,
                         const std::shared_ptr<const db::connection_config>& database_config,
                         std::string_view template_root) -> void;

/// @brief Registers `GET /tasks`, the all-tasks page the curriculum links to.
///
/// @param store The settings the page shell reads its site name from.
/// @param database_config The pool the session, the curriculum and the tasks are read through.
/// @param template_root The directory the template is rendered from.
///
/// A request without a live approved session is redirected to `/signin`, the same answer the task page
/// gives, because the page is a learner's surface rather than a public one. The list applies the same
/// published-and-visible predicate the task page does, so it can only link a task the page would serve.
auto register_task_list_route(const std::shared_ptr<SettingsStore>& store,
                              const std::shared_ptr<const db::connection_config>& database_config,
                              std::string_view template_root) -> void;

/// @brief Registers `POST /level`, the one write a learner makes to their own account.
///
/// @param database_config The pool the session, the vocabulary and the write are read and made through.
///
/// The rung id travels in the body and is checked against the vocabulary before the update, so a value the
/// list does not define is refused with its name rather than by a foreign key. An administrator is refused:
/// the column is a learner's working level, and no other surface offers it.
auto register_level_route(const std::shared_ptr<const db::connection_config>& database_config) -> void;

/// @brief Registers `GET /settings`, the admin settings page.
///
/// @param store The settings the page shell and the rendered values are read from.
/// @param database_config The pool the session and its authority are read through.
/// @param view The cached visible-settings list; a secret is already redacted in it.
/// @param template_root The directory the template is rendered from.
///
/// An unauthenticated request is redirected to `/signin`: it is not forbidden, it is unauthenticated. A
/// session without `settings_read` is answered 403.
auto register_settings_route(const std::shared_ptr<SettingsStore>& store,
                             const std::shared_ptr<const db::connection_config>& database_config,
                             const std::shared_ptr<const SettingsView>& view,
                             std::string_view template_root) -> void;

}  // namespace cms::api
