#include "task_manifest.hpp"

#include <array>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <simdjson.h>

#include "detail.hpp"

namespace cms::bank {
namespace {

using detail::known;
using detail::required_integer;
using detail::required_text;
using detail::with_json;

constexpr std::string_view TASK_FILE = "task.json";

constexpr std::array<std::string_view, 4> TASK_STATUSES{"draft", "verified", "published",
                                                        "rejected"};
constexpr std::array<std::string_view, 4> CHECKER_KINDS{"standard", "token", "testlib",
                                                        "interactive"};
constexpr std::array<std::string_view, 3> ORIGIN_KINDS{"original", "imported", "ai"};
constexpr std::int64_t MINIMUM_DIFFICULTY = 800;
constexpr std::int64_t MAXIMUM_DIFFICULTY = 3500;
constexpr std::int64_t DIFFICULTY_STEP = 100;

auto nested(const simdjson::dom::object& object, std::string_view name)
    -> std::optional<simdjson::dom::object> {
  auto child = simdjson::dom::object{};
  if (object.at_key(name).get_object().get(child) != simdjson::SUCCESS) {
    return std::nullopt;
  }
  return child;
}

/// @return The integer @p name holds, nothing when the key is absent, or a refusal naming @p name when it holds
/// another type: a typo is never silently an absent bound. The caller adds the place it read.
auto strict_integer(const simdjson::dom::object& object, std::string_view name)
    -> Result<std::optional<std::int64_t>> {
  auto element = simdjson::dom::element{};
  if (object.at_key(name).get(element) != simdjson::SUCCESS) {
    return std::optional<std::int64_t>{};
  }
  auto value = std::int64_t{0};
  if (element.get_int64().get(value) != simdjson::SUCCESS) {
    return std::unexpected(Error::make(ErrorCode::invalid_argument, std::string{name} + " is not an integer"));
  }
  return std::optional<std::int64_t>{value};
}

auto parse_identity(const simdjson::dom::object& object, std::string_view where, Task& task)
    -> Result<void> {
  const auto id = required_text(object, "id", where);
  if (!id) {
    return std::unexpected(id.error());
  }
  task.id = *id;

  const auto version = required_integer(object, "version", where);
  if (!version) {
    return std::unexpected(version.error());
  }
  task.version = static_cast<std::int32_t>(*version);

  const auto status = required_text(object, "status", where);
  if (!status) {
    return std::unexpected(status.error());
  }
  task.status = *status;
  return known(task.status, TASK_STATUSES, "status", where);
}


auto parse_titles(const simdjson::dom::object& object, std::string_view where, Task& task)
    -> Result<void> {
  const auto title = nested(object, "title");
  if (!title) {
    return std::unexpected(
        Error::make(ErrorCode::invalid_argument, std::string{where} + ": title is missing"));
  }
  const auto english = required_text(*title, "en", where);
  if (!english) {
    return std::unexpected(english.error());
  }
  const auto thai = required_text(*title, "th", where);
  if (!thai) {
    return std::unexpected(thai.error());
  }
  task.title_en = *english;
  task.title_th = *thai;
  return {};
}


auto parse_topics(const simdjson::dom::object& object, std::string_view where, Task& task)
    -> Result<void> {
  auto array = simdjson::dom::array{};
  if (object.at_key("topics").get_array().get(array) != simdjson::SUCCESS) {
    return std::unexpected(
        Error::make(ErrorCode::invalid_argument, std::string{where} + ": topics is missing"));
  }
  for (const auto element : array) {
    auto view = std::string_view{};
    if (element.get_string().get(view) != simdjson::SUCCESS) {
      return std::unexpected(Error::make(
          ErrorCode::invalid_argument, std::string{where} + ": a topic is not a string"));
    }
    task.topics.emplace_back(view);
  }
  return {};
}


auto parse_difficulty(const simdjson::dom::object& object, std::string_view where, Task& task)
    -> Result<void> {
  const auto difficulty = required_integer(object, "difficulty", where);
  if (!difficulty) {
    return std::unexpected(difficulty.error());
  }
  if (*difficulty < MINIMUM_DIFFICULTY || *difficulty > MAXIMUM_DIFFICULTY ||
      *difficulty % DIFFICULTY_STEP != 0) {
    return std::unexpected(Error::make(
        ErrorCode::invalid_argument,
        std::string{where} + ": difficulty must be a multiple of " + std::to_string(DIFFICULTY_STEP) +
            " between " + std::to_string(MINIMUM_DIFFICULTY) + " and " +
            std::to_string(MAXIMUM_DIFFICULTY)));
  }
  task.difficulty = static_cast<std::int32_t>(*difficulty);
  return {};
}


auto parse_limits(const simdjson::dom::object& object, std::string_view where, Task& task)
    -> Result<void> {
  const auto limits = nested(object, "limits");
  if (!limits) {
    return std::unexpected(
        Error::make(ErrorCode::invalid_argument, std::string{where} + ": limits is missing"));
  }
  const auto time = required_integer(*limits, "time_ms", where);
  if (!time) {
    return std::unexpected(time.error());
  }
  const auto memory = required_integer(*limits, "memory_mb", where);
  if (!memory) {
    return std::unexpected(memory.error());
  }
  task.time_limit_ms = static_cast<std::int32_t>(*time);
  task.memory_limit_mb = static_cast<std::int32_t>(*memory);
  return {};
}


/// The task type and the score mode the content declares, both optional: content that predates the authoring form
/// names neither, and keeps loading.
auto parse_authoring(const simdjson::dom::object& object, std::string_view where, Task& task)
    -> Result<void> {
  (void)where;
  task.task_type = detail::field_text(object, "task_type").value_or("");
  task.score_mode = detail::field_text(object, "score_mode").value_or("");
  return {};
}

constexpr std::int64_t DEFAULT_POINTS = 100;
constexpr std::int64_t MINIMUM_POINTS = 1;
constexpr std::int64_t MAXIMUM_POINTS = 1000;

/// The total a task is scored out of. Absent means the default, so the content that predates subtasks keeps loading.
auto parse_points(const simdjson::dom::object& object, std::string_view where, Task& task)
    -> Result<void> {
  task.points = static_cast<std::int32_t>(DEFAULT_POINTS);
  const auto points = detail::field_integer(object, "points");
  if (!points.has_value()) {
    return {};
  }
  if (*points < MINIMUM_POINTS || *points > MAXIMUM_POINTS) {
    return std::unexpected(detail::fail(
        where, "points must be between " + std::to_string(MINIMUM_POINTS) + " and " +
                   std::to_string(MAXIMUM_POINTS)));
  }
  task.points = static_cast<std::int32_t>(*points);
  return {};
}

/// The task's own bounds, which every subtask narrows (ADR 0041). Absent for a task that carries no subtask.
auto parse_bounds(const simdjson::dom::object& object, std::string_view where, Task& task) -> Result<void> {
  const auto bounds = nested(object, "bounds");
  if (!bounds) {
    return {};
  }
  const auto min_n = strict_integer(*bounds, "min_n");
  if (!min_n) {
    return std::unexpected(detail::fail(where, min_n.error().message));
  }
  const auto max_n = strict_integer(*bounds, "max_n");
  if (!max_n) {
    return std::unexpected(detail::fail(where, max_n.error().message));
  }
  const auto value_min = strict_integer(*bounds, "value_min");
  if (!value_min) {
    return std::unexpected(detail::fail(where, value_min.error().message));
  }
  const auto value_max = strict_integer(*bounds, "value_max");
  if (!value_max) {
    return std::unexpected(detail::fail(where, value_max.error().message));
  }
  auto missing = std::string{};
  const auto note = [&missing](std::string_view name, std::optional<std::int64_t> value) {
    if (value.has_value()) {
      return;
    }
    missing += missing.empty() ? "" : ", ";
    missing += name;
  };
  note("min_n", *min_n);
  note("max_n", *max_n);
  note("value_min", *value_min);
  note("value_max", *value_max);
  if (!missing.empty()) {
    return std::unexpected(detail::fail(where, "the task's bounds are missing " + missing));
  }
  task.bounds = cms::TaskBounds{
      .min_n = **min_n, .max_n = **max_n, .value_min = **value_min, .value_max = **value_max};
  return {};
}

/// The scoring groups. An absent array is a task with none, which is the only shape the content had before ADR 0033;
/// a subtask names its position, its points and its typed narrowing of the task's bounds.
auto parse_subtasks(const simdjson::dom::object& object, std::string_view where, Task& task)
    -> Result<void> {
  auto array = simdjson::dom::array{};
  if (object.at_key("subtasks").get_array().get(array) != simdjson::SUCCESS) {
    return {};
  }
  for (const auto element : array) {
    auto entry = simdjson::dom::object{};
    if (element.get_object().get(entry) != simdjson::SUCCESS) {
      return std::unexpected(detail::fail(where, "a subtask is not an object"));
    }
    const auto position = required_integer(entry, "position", where);
    if (!position) {
      return std::unexpected(position.error());
    }
    const auto points = required_integer(entry, "points", where);
    if (!points) {
      return std::unexpected(points.error());
    }
    if (*position < 1) {
      return std::unexpected(detail::fail(where, "a subtask position counts from one"));
    }
    if (*points < MINIMUM_POINTS || *points > MAXIMUM_POINTS) {
      return std::unexpected(detail::fail(
          where, "a subtask's points must be between " + std::to_string(MINIMUM_POINTS) + " and " +
                     std::to_string(MAXIMUM_POINTS)));
    }
    for (const auto& seen : task.subtasks) {
      if (seen.position == *position) {
        return std::unexpected(detail::fail(
            where, "subtask position " + std::to_string(*position) + " is named twice"));
      }
    }
    if (!task.bounds.has_value()) {
      return std::unexpected(detail::fail(
          where, "a subtask narrows the task's bounds, and the task declares none"));
    }
    auto bounds = cms::SubtaskBounds{};
    for (const auto& [name, target] : {std::pair{"min_n", &bounds.min_n}, std::pair{"max_n", &bounds.max_n},
                                       std::pair{"value_min", &bounds.value_min},
                                       std::pair{"value_max", &bounds.value_max}}) {
      const auto value = strict_integer(entry, name);
      if (!value) {
        return std::unexpected(detail::fail(
            where, "subtask " + std::to_string(*position) + ": " + value.error().message));
      }
      *target = *value;
    }
    if (const auto refused = cms::narrowing_refusal(bounds, *task.bounds); refused.has_value()) {
      return std::unexpected(detail::fail(
          where, "subtask " + std::to_string(*position) + ": " + *refused));
    }
    task.subtasks.push_back(Subtask{.position = static_cast<std::int32_t>(*position),
                                    .points = static_cast<std::int32_t>(*points),
                                    .bounds = bounds});
  }
  return {};
}

auto parse_checker(const simdjson::dom::object& object, std::string_view where, Task& task)
    -> Result<void> {
  const auto checker = nested(object, "checker");
  if (!checker) {
    return std::unexpected(
        Error::make(ErrorCode::invalid_argument, std::string{where} + ": checker is missing"));
  }
  const auto kind = required_text(*checker, "kind", where);
  if (!kind) {
    return std::unexpected(kind.error());
  }
  task.checker_kind = *kind;
  const auto allowed = known(task.checker_kind, CHECKER_KINDS, "checker.kind", where);
  if (!allowed) {
    return allowed;
  }
  task.checker_path = detail::field_text(*checker, "path").value_or("");
  if (task.checker_kind == "testlib" && task.checker_path.empty()) {
    return std::unexpected(Error::make(
        ErrorCode::invalid_argument, std::string{where} + ": a testlib checker names its path"));
  }
  if (task.checker_kind != "testlib" && !task.checker_path.empty()) {
    return std::unexpected(Error::make(
        ErrorCode::invalid_argument,
        std::string{where} + ": only a testlib checker carries a path"));
  }
  return {};
}

/// ADR 0046 §2 retired the file's verification claim: what ran is the certificate row's evidence. A file that still
/// names one is refused here rather than read as a task with no claim, so an old draft cannot reach the database as
/// verified by saying so.
auto refuse_retired_verification(const simdjson::dom::object& object, std::string_view where) -> Result<void> {
  const auto verification = nested(object, "verification");
  if (!verification) {
    return {};
  }
  for (const auto retired : {"status", "methods", "verified_at"}) {
    auto element = simdjson::dom::element{};
    if (verification->at_key(retired).get(element) == simdjson::SUCCESS) {
      return std::unexpected(detail::fail(
          where, "verification names \"" + std::string{retired} +
                     "\", which ADR 0046 §2 retired: a task's verified state is the certificate row's, so the file "
                     "cannot say it"));
    }
  }
  return {};
}

auto parse_origin(const simdjson::dom::object& object, std::string_view where, Task& task)
    -> Result<void> {
  const auto origin = nested(object, "origin");
  if (!origin) {
    return std::unexpected(
        Error::make(ErrorCode::invalid_argument, std::string{where} + ": origin is missing"));
  }
  const auto origin_kind = required_text(*origin, "kind", where);
  if (!origin_kind) {
    return std::unexpected(origin_kind.error());
  }
  task.origin_kind = *origin_kind;
  return known(task.origin_kind, ORIGIN_KINDS, "origin.kind", where);
}


}  // namespace


auto parse_manifest(const std::filesystem::path& directory, Task& task) -> Result<void> {
  const auto where = directory.filename().string();
  const auto parsed = with_json(directory / TASK_FILE, [&](const simdjson::dom::object& object) {
    if (const auto step = parse_identity(object, where, task); !step) {
      return step;
    }
    if (const auto step = parse_titles(object, where, task); !step) {
      return step;
    }
    if (const auto step = parse_topics(object, where, task); !step) {
      return step;
    }
    if (const auto step = parse_difficulty(object, where, task); !step) {
      return step;
    }
    if (const auto step = parse_limits(object, where, task); !step) {
      return step;
    }
    if (const auto step = parse_authoring(object, where, task); !step) {
      return step;
    }
    if (const auto step = parse_points(object, where, task); !step) {
      return step;
    }
    if (const auto step = parse_bounds(object, where, task); !step) {
      return step;
    }
    if (const auto step = parse_subtasks(object, where, task); !step) {
      return step;
    }
    if (const auto step = parse_checker(object, where, task); !step) {
      return step;
    }
    if (const auto step = refuse_retired_verification(object, where); !step) {
      return step;
    }
    return parse_origin(object, where, task);
  });
  if (!parsed) {
    return std::unexpected(parsed.error());
  }
  return {};
}

}  // namespace cms::bank
