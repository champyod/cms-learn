#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <cms/api/actor.hpp>
#include <cms/db/connection.hpp>
#include <cms/error.hpp>

namespace cms::api {

/// @brief One field a form must render, as the definition row holds it (ADR 0024).
struct FieldDefinitionRow {
  std::string key;
  std::string kind;
  std::string label_en;
  std::string label_th;
  bool is_required{false};
  std::vector<std::string> choices;
  std::optional<std::int32_t> minimum;
  std::optional<std::int32_t> maximum;
  std::int32_t position{0};
};

/// @brief One task type with the fields it declares, in position order.
struct TaskTypeRow {
  std::string id;
  std::string title_en;
  std::string title_th;
  std::vector<FieldDefinitionRow> fields;
};

/// @brief The keys the form itself owns: never a field the definitions can shadow, and never a column the
/// author may set (status, visibility, seed, version, id, actor are not among them).
inline constexpr std::string_view TYPE_KEY = "type_id";
inline constexpr std::string_view TITLE_EN_KEY = "title_en";
inline constexpr std::string_view TITLE_TH_KEY = "title_th";
inline constexpr std::string_view DIFFICULTY_KEY = "difficulty";

/// @brief Reads every task type and its fields, ordered by position, for a form to render.
[[nodiscard]] Result<std::vector<TaskTypeRow>> read_task_types(db::connection& database);

/// @brief Saves a draft from the body of the authoring form.
///
/// @param database The connection; the caller owns the transaction.
/// @param actor The account acting, produced by the panel session.
/// @param body The form's body, as the browser sent it.
/// @return The new draft's id and one line a caller may show.
///
/// The body may name only the form's own keys and the fields its type defines; every other key is refused by
/// name before any statement runs, which is what stops a request from setting a status, a version, an id, a
/// visibility, a seed or an actor. A value the definition forbids is refused by the field's name, also before
/// anything is written. The row is created as a draft, never published: publication needs machine evidence
/// (`check:publish`), and a form that could publish would be the defect that check exists to prevent.
[[nodiscard]] Result<std::string> save_task_draft(db::connection& database, const Actor& actor,
                                                  std::string_view body);

}  // namespace cms::api
