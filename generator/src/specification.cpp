#include <cms/generate/specification.hpp>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

#include <simdjson.h>

#include "json.hpp"

#include <cms/db/connection.hpp>
#include <cms/db/tables.hpp>
#include <cms/db/transaction.hpp>
#include <cms/generate/data_frame.hpp>
#include <cms/object_store.hpp>

namespace cms::generate {
namespace {

using simdjson::dom::element;
using simdjson::dom::object;

constexpr std::string_view ARRAY_OF_INTEGERS = "n_then_values";
constexpr std::string_view SPECIFICATION_KIND = "specification";
constexpr std::string_view ORACLE_KIND = "oracle";
constexpr std::int16_t PROOF_STEP = 3;
constexpr std::int64_t BYTES_PER_VALUE = 8;
constexpr std::int64_t BYTES_PER_MB = 1024 * 1024;

/// @return @p names as one list, so a refusal can name every missing fact in one message.
auto joined(const std::vector<std::string>& names) -> std::string {
  auto text = std::string{};
  for (std::size_t index = 0; index < names.size(); ++index) {
    text += index == 0 ? "" : ", ";
    text += names[index];
  }
  return text;
}

auto refuse(std::string_view message) -> Error {
  return Error::make(ErrorCode::invalid_argument, message);
}

}  // namespace

auto read_bounds(const simdjson::dom::object& object, cms::TaskBounds& bounds) -> Result<void> {
  auto values = simdjson::dom::object{};
  if (object.at_key("bounds").get_object().get(values) != simdjson::SUCCESS) {
    return std::unexpected(refuse("the specification answer has no bounds object"));
  }
  const auto min_n = detail::field_integer(values, "min_n");
  const auto max_n = detail::field_integer(values, "max_n");
  const auto value_min = detail::field_integer(values, "value_min");
  const auto value_max = detail::field_integer(values, "value_max");
  auto missing = std::vector<std::string>{};
  if (!min_n) {
    missing.emplace_back("min_n");
  }
  if (!max_n) {
    missing.emplace_back("max_n");
  }
  if (!value_min) {
    missing.emplace_back("value_min");
  }
  if (!value_max) {
    missing.emplace_back("value_max");
  }
  if (!missing.empty()) {
    return std::unexpected(refuse("the specification's bounds are missing " + joined(missing)));
  }
  if (*min_n > *max_n) {
    return std::unexpected(refuse("the specification's bounds are inverted: min_n " +
                                  std::to_string(*min_n) + " is above max_n " + std::to_string(*max_n)));
  }
  if (*value_min > *value_max) {
    return std::unexpected(refuse("the specification's bounds are inverted: value_min " +
                                  std::to_string(*value_min) + " is above value_max " +
                                  std::to_string(*value_max)));
  }
  bounds = cms::TaskBounds{.min_n = *min_n, .max_n = *max_n, .value_min = *value_min, .value_max = *value_max};
  return {};
}

/// @return The bound @p name holds, nothing when it is absent, or a refusal when it holds another type: a bound a
/// reader cannot read is never silently the task's value.
auto bound_member(const simdjson::dom::object& subtask, std::string_view name)
    -> Result<std::optional<std::int64_t>> {
  auto element = simdjson::dom::element{};
  if (subtask.at_key(name).get(element) != simdjson::SUCCESS) {
    return std::optional<std::int64_t>{};
  }
  auto value = std::int64_t{0};
  if (element.get_int64().get(value) != simdjson::SUCCESS) {
    return std::unexpected(refuse("a subtask's " + std::string{name} + " is not an integer"));
  }
  return std::optional<std::int64_t>{value};
}

auto read_subtasks(const simdjson::dom::object& object, std::vector<SubtaskConstraint>& into)
    -> Result<void> {
  auto entries = simdjson::dom::array{};
  if (object.at_key("subtasks").get_array().get(entries) != simdjson::SUCCESS) {
    return {};
  }
  for (const auto entry : entries) {
    auto subtask = simdjson::dom::object{};
    if (entry.get_object().get(subtask) != simdjson::SUCCESS) {
      return std::unexpected(refuse("a subtask is not a JSON object"));
    }
    const auto position = detail::field_integer(subtask, "position");
    if (!position) {
      return std::unexpected(refuse("a subtask names no position"));
    }
    auto bounds = cms::SubtaskBounds{};
    for (const auto& [name, target] : {std::pair{"min_n", &bounds.min_n}, std::pair{"max_n", &bounds.max_n},
                                       std::pair{"value_min", &bounds.value_min},
                                       std::pair{"value_max", &bounds.value_max}}) {
      const auto value = bound_member(subtask, name);
      if (!value) {
        return std::unexpected(value.error());
      }
      *target = *value;
    }
    into.push_back(SubtaskConstraint{.position = static_cast<std::int32_t>(*position), .bounds = bounds});
  }
  return {};
}

auto parse_specification_answer(std::string_view answer, const ModelCall& call) -> Result<Step3Answer> {
  auto parsed = Step3Answer{};
  parsed.call = call;
  parsed.answer_json = std::string{answer};
  const auto used = detail::with_object(
      answer, "the specification answer", [&](const simdjson::dom::object& object) -> Result<void> {
        const auto input = detail::field_text(object, "input");
        const auto output = detail::field_text(object, "output");
        const auto shape = detail::field_text(object, "input_shape");
        auto missing = std::vector<std::string>{};
        if (!input) {
          missing.emplace_back("input");
        }
        if (!output) {
          missing.emplace_back("output");
        }
        if (!shape) {
          missing.emplace_back("input_shape");
        }
        if (!missing.empty()) {
          return std::unexpected(refuse("the specification answer is missing " + joined(missing)));
        }
        if (*shape != ARRAY_OF_INTEGERS) {
          return std::unexpected(refuse("the specification's input shape \"" + *shape +
                                        "\" is not one this step can sample; it samples \"n_then_values\""));
        }
        parsed.specification.input = *input;
        parsed.specification.output = *output;
        const auto bounds = read_bounds(object, parsed.specification.bounds);
        if (!bounds) {
          return std::unexpected(bounds.error());
        }
        const auto subtasks = read_subtasks(object, parsed.specification.subtasks);
        if (!subtasks) {
          return std::unexpected(subtasks.error());
        }
        for (const auto& subtask : parsed.specification.subtasks) {
          if (const auto refused = cms::narrowing_refusal(subtask.bounds, parsed.specification.bounds);
              refused.has_value()) {
            return std::unexpected(refuse("subtask " + std::to_string(subtask.position) + ": " + *refused));
          }
        }
        const auto oracle = detail::field_text(object, "oracle");
        if (!oracle || oracle->empty()) {
          return std::unexpected(refuse("the specification answer carries no oracle program to check"));
        }
        parsed.oracle_source = *oracle;
        return {};
      });
  if (!used) {
    return std::unexpected(used.error());
  }
  return parsed;
}

auto specification_prompt(const CandidatePlan& chosen) -> Result<std::string> {
  const auto framed = frame_data(ModelData{.label = "the chosen plan", .text = chosen.plan_json});
  if (!framed) {
    return std::unexpected(framed.error());
  }
  return "You are step 3 of a task-generation pipeline. The chosen plan is below as data. Read it into a\n"
         "specification, and write a brute-force oracle program that answers the specification exactly.\n\n" +
         *framed +
         "\n\nAnswer with one JSON object and nothing else:\n"
         "{\"input\": \"<what the program reads, in words>\",\n"
         " \"output\": \"<what it writes, in words>\",\n"
         " \"input_shape\": \"n_then_values\",\n"
         " \"bounds\": {\"min_n\": <int>, \"max_n\": <int>, \"value_min\": <int>, \"value_max\": <int>},\n"
         " \"subtasks\": [{\"position\": <int>, \"min_n\": <int?>, \"max_n\": <int?>, \"value_min\": <int?>,\n"
         "               \"value_max\": <int?>}],\n"
         " \"oracle\": \"<a complete C++17 program: reads the input shape, prints the answer>\"}\n"
         "A subtask's bounds narrow the task's own: every field is optional, an absent field is the task's value, at\n"
         "least one field must be present, and no field may leave the task's bound.\n";
}

auto specify_step3(ModelClient& client, const CandidatePlan& chosen) -> Result<Step3Answer> {
  const auto prompt = specification_prompt(chosen);
  if (!prompt) {
    return std::unexpected(prompt.error());
  }
  const auto completion = client.complete(*prompt);
  if (!completion) {
    return std::unexpected(completion.error());
  }
  auto answer = parse_specification_answer(completion->text, completion->call);
  if (!answer) {
    return std::unexpected(answer.error());
  }
  const auto limit = derive_limit(chosen.intended_complexity, answer->specification.bounds);
  if (!limit) {
    return std::unexpected(limit.error());
  }
  answer->limit = *limit;
  return answer;
}

}  // namespace cms::generate
