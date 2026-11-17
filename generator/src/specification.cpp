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

auto sample_smallest(const Specification& specification) -> Result<std::string> {
  if (specification.bounds.min_n <= 0) {
    return std::unexpected(refuse("the bounds declare no smallest n to sample the oracle with"));
  }
  const auto count = specification.bounds.min_n;
  auto input = std::to_string(count) + "\n";
  for (std::int64_t index = 0; index < count; ++index) {
    input += std::to_string(specification.bounds.value_min);
    input += index + 1 == count ? '\n' : ' ';
  }
  return input;
}

auto read_bounds(const simdjson::dom::object& object, Bounds& bounds) -> Result<void> {
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
  bounds = Bounds{.min_n = *min_n, .max_n = *max_n, .value_min = *value_min, .value_max = *value_max};
  return {};
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
    const auto constraint = detail::field_text(subtask, "constraint");
    if (!position || !constraint) {
      return std::unexpected(refuse("a subtask is missing position or constraint"));
    }
    into.push_back(
        SubtaskConstraint{.position = static_cast<std::int32_t>(*position), .constraint = *constraint});
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
         " \"subtasks\": [{\"position\": <int>, \"constraint\": \"<the constraint that defines this group>\"}],\n"
         " \"oracle\": \"<a complete C++17 program: reads the input shape, prints the answer>\"}\n";
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
