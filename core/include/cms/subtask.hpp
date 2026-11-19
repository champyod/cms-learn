#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace cms {

/// @brief The task's own bounds: the sizes and values every case is drawn from.
struct TaskBounds {
  std::int64_t min_n{0};
  std::int64_t max_n{0};
  std::int64_t value_min{0};
  std::int64_t value_max{0};
};

/// @brief A subtask's constraint as a typed narrowing of the task's bounds (ADR 0041).
///
/// An absent field is the task's value: the subtask narrows what it names and leaves the rest as the task declared
/// it. The sentence a teacher and a student read is rendered from these fields, and the sampler draws from them, so
/// the two cannot say different things.
struct SubtaskBounds {
  std::optional<std::int64_t> min_n{};
  std::optional<std::int64_t> max_n{};
  std::optional<std::int64_t> value_min{};
  std::optional<std::int64_t> value_max{};
};

/// @return Whether @p bounds names no narrowing at all, which is a subtask that is the whole task.
[[nodiscard]] inline auto is_empty(const SubtaskBounds& bounds) -> bool {
  return !bounds.min_n.has_value() && !bounds.max_n.has_value() && !bounds.value_min.has_value() &&
         !bounds.value_max.has_value();
}

/// @return @p bounds with every absent field filled from @p task, which is the interval the subtask admits.
[[nodiscard]] inline auto narrowed(const SubtaskBounds& bounds, const TaskBounds& task) -> TaskBounds {
  return TaskBounds{.min_n = bounds.min_n.value_or(task.min_n),
                    .max_n = bounds.max_n.value_or(task.max_n),
                    .value_min = bounds.value_min.value_or(task.value_min),
                    .value_max = bounds.value_max.value_or(task.value_max)};
}

/// @brief Refuses a narrowing that names nothing, leaves the task's interval, or admits no case.
///
/// @param bounds The subtask's typed constraint.
/// @param task The task's own bounds.
/// @return The refusal naming every bound at fault, or nothing when @p bounds is a real narrowing of @p task.
[[nodiscard]] inline auto narrowing_refusal(const SubtaskBounds& bounds, const TaskBounds& task)
    -> std::optional<std::string> {
  if (is_empty(bounds)) {
    return "the subtask narrows nothing: a subtask that admits every case the task admits is the whole task";
  }
  auto faults = std::string{};
  const auto fault = [&faults](const std::string& message) {
    faults += faults.empty() ? "" : "; ";
    faults += message;
  };
  if (bounds.min_n.has_value() && *bounds.min_n < task.min_n) {
    fault("min_n " + std::to_string(*bounds.min_n) + " is below the task's " + std::to_string(task.min_n));
  }
  if (bounds.max_n.has_value() && *bounds.max_n > task.max_n) {
    fault("max_n " + std::to_string(*bounds.max_n) + " is above the task's " + std::to_string(task.max_n));
  }
  if (bounds.value_min.has_value() && *bounds.value_min < task.value_min) {
    fault("value_min " + std::to_string(*bounds.value_min) + " is below the task's " +
          std::to_string(task.value_min));
  }
  if (bounds.value_max.has_value() && *bounds.value_max > task.value_max) {
    fault("value_max " + std::to_string(*bounds.value_max) + " is above the task's " +
          std::to_string(task.value_max));
  }
  if (!faults.empty()) {
    return faults;
  }
  const auto admitted = narrowed(bounds, task);
  if (admitted.min_n > admitted.max_n) {
    return "the subtask admits no n: its min_n " + std::to_string(admitted.min_n) + " is above its max_n " +
           std::to_string(admitted.max_n);
  }
  if (admitted.value_min > admitted.value_max) {
    return "the subtask admits no value: its value_min " + std::to_string(admitted.value_min) +
           " is above its value_max " + std::to_string(admitted.value_max);
  }
  return std::nullopt;
}

/// @return Whether @p bounds is a real narrowing of @p task: it names something, stays inside the task's interval,
/// and admits at least one case.
[[nodiscard]] inline auto is_within(const SubtaskBounds& bounds, const TaskBounds& task) -> bool {
  return !narrowing_refusal(bounds, task).has_value();
}

/// @return The sentence the bounds say, e.g. "n ≥ 1, n ≤ 2000, a ≤ 1000000000".
///
/// It is a formula, not prose: digits, comparison signs, and the two symbols the statement already uses (`n` for the
/// size, `a` for a value). It therefore reads the same in every locale, and the words around it — the heading, the
/// lead line, the column names — are rendered per locale by the caller. Nothing here is a translation of another
/// language's text, which is what ADR 0039 forbids.
[[nodiscard]] inline auto bounds_text(const SubtaskBounds& bounds) -> std::string {
  auto text = std::string{};
  const auto part = [&text](const std::string& piece) {
    text += text.empty() ? "" : ", ";
    text += piece;
  };
  if (bounds.min_n.has_value()) {
    part("n \u2265 " + std::to_string(*bounds.min_n));
  }
  if (bounds.max_n.has_value()) {
    part("n \u2264 " + std::to_string(*bounds.max_n));
  }
  if (bounds.value_min.has_value()) {
    part("a \u2265 " + std::to_string(*bounds.value_min));
  }
  if (bounds.value_max.has_value()) {
    part("a \u2264 " + std::to_string(*bounds.value_max));
  }
  return text;
}

}  // namespace cms
