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
constexpr std::array<std::string_view, 3> VERIFICATION_STATUSES{"pending", "verified", "failed"};
constexpr std::array<std::string_view, 4> CHECKER_KINDS{"standard", "token", "testlib",
                                                        "interactive"};
constexpr std::array<std::string_view, 3> ORIGIN_KINDS{"original", "imported", "ai"};
constexpr std::array<std::string_view, 2> LOCALES{"en", "th"};
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

auto parse_origin_and_verification(const simdjson::dom::object& object, std::string_view where,
                                   Task& task) -> Result<void> {
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

  const auto verification = nested(object, "verification");
  if (!verification) {
    return std::unexpected(Error::make(ErrorCode::invalid_argument,
                                       std::string{where} + ": verification is missing"));
  }
  const auto verification_status = required_text(*verification, "status", where);
  if (!verification_status) {
    return std::unexpected(verification_status.error());
  }
  task.verification_status = *verification_status;

  const auto origin_ok = known(task.origin_kind, ORIGIN_KINDS, "origin.kind", where);
  if (!origin_ok) {
    return origin_ok;
  }
  return known(task.verification_status, VERIFICATION_STATUSES, "verification.status", where);
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
    if (const auto step = parse_checker(object, where, task); !step) {
      return step;
    }
    return parse_origin_and_verification(object, where, task);
  });
  if (!parsed) {
    return std::unexpected(parsed.error());
  }
  return {};
}

}  // namespace cms::bank
