#include <cms/generate/specification.hpp>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <simdjson.h>

#include <sqlpp23/sqlpp23.h>

#include <cms/db/connection.hpp>
#include <cms/db/tables.hpp>
#include <cms/db/transaction.hpp>
#include <cms/generate/artefact.hpp>
#include <cms/object_store.hpp>

#include "json.hpp"

namespace cms::generate {
namespace {

using detail::field_integer;
using detail::field_text;

constexpr std::string_view NO_ANSWER = "step 3 recorded no answer";
constexpr std::int16_t PROOF_STEP = 3;

/// The stored specification's keys, declared once: @ref specification_json writes them and @ref read_specification
/// requires them, so a key cannot drift between the two halves of one shape.
constexpr std::string_view KEY_INPUT = "input";
constexpr std::string_view KEY_OUTPUT = "output";
constexpr std::string_view KEY_INPUT_SHAPE = "input_shape";
constexpr std::string_view KEY_BOUNDS = "bounds";
constexpr std::string_view KEY_MIN_N = "min_n";
constexpr std::string_view KEY_MAX_N = "max_n";
constexpr std::string_view KEY_VALUE_MIN = "value_min";
constexpr std::string_view KEY_VALUE_MAX = "value_max";
constexpr std::string_view KEY_SUBTASKS = "subtasks";
constexpr std::string_view KEY_POSITION = "position";
constexpr std::string_view KEY_CONSTRAINT = "constraint";
constexpr std::string_view N_THEN_VALUES = "n_then_values";

/// One row per model call (ADR 0030), with the step's own step and kind.
constexpr std::string_view RECORD_ARTEFACT = R"(
INSERT INTO generation_artefacts (job_id, step, kind, object_key, inline_text, digest, model_id, prompt_version,
                                  seed, call_index)
VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10)
ON CONFLICT (job_id, step, kind, locale, call_index) DO UPDATE
SET object_key = excluded.object_key, inline_text = excluded.inline_text, digest = excluded.digest,
    model_id = excluded.model_id, prompt_version = excluded.prompt_version, seed = excluded.seed
RETURNING id
)";

constexpr std::string_view RECORD_SPECIFICATION = R"(
INSERT INTO generation_specifications (job_id, specification, time_limit_ms, memory_limit_mb, derivation,
                                      answer_artefact_id, oracle_artefact_id)
VALUES ($1, $2::jsonb, $3, $4, $5, $6, $7)
ON CONFLICT (job_id) DO UPDATE
SET specification = excluded.specification, time_limit_ms = excluded.time_limit_ms,
    memory_limit_mb = excluded.memory_limit_mb, derivation = excluded.derivation,
    answer_artefact_id = excluded.answer_artefact_id, oracle_artefact_id = excluded.oracle_artefact_id
)";

auto refuse(std::string_view message) -> Error {
  return Error::make(ErrorCode::invalid_argument, message);
}

/// @return @p text with the characters a JSON string cannot hold raw, escaped.
auto escaped(std::string_view text) -> std::string {
  auto out = std::string{};
  out.reserve(text.size());
  for (const auto character : text) {
    if (character == '"' || character == '\\') {
      out.push_back('\\');
    }
    if (character == '\n') {
      out += "\\n";
      continue;
    }
    out.push_back(character);
  }
  return out;
}

auto text_member(std::string_view key, std::string_view value) -> std::string {
  return "\"" + std::string{key} + "\":\"" + escaped(value) + "\"";
}

auto integer_member(std::string_view key, std::int64_t value) -> std::string {
  return "\"" + std::string{key} + "\":" + std::to_string(value);
}

auto object_member(std::string_view key, std::string_view value) -> std::string {
  return "\"" + std::string{key} + "\":" + std::string{value};
}

auto bounds_json(const Bounds& bounds) -> std::string {
  return "{" + integer_member(KEY_MIN_N, bounds.min_n) + "," + integer_member(KEY_MAX_N, bounds.max_n) + "," +
         integer_member(KEY_VALUE_MIN, bounds.value_min) + "," + integer_member(KEY_VALUE_MAX, bounds.value_max) + "}";
}

auto subtasks_json(const std::vector<SubtaskConstraint>& subtasks) -> std::string {
  auto json = std::string{"["};
  for (std::size_t index = 0; index < subtasks.size(); ++index) {
    json += index == 0 ? "" : ",";
    json += "{" + integer_member(KEY_POSITION, subtasks[index].position) + "," +
            text_member(KEY_CONSTRAINT, subtasks[index].constraint) + "}";
  }
  json += ']';
  return json;
}

auto listed(const std::vector<std::string>& names) -> std::string {
  auto text = std::string{};
  for (std::size_t index = 0; index < names.size(); ++index) {
    text += index == 0 ? "" : ", ";
    text += names[index];
  }
  return text;
}

auto read_bounds_json(const simdjson::dom::object& object, Bounds& bounds, std::vector<std::string>& missing)
    -> void {
  auto values = simdjson::dom::object{};
  if (object.at_key(KEY_BOUNDS).get_object().get(values) != simdjson::SUCCESS) {
    missing.emplace_back(KEY_BOUNDS);
    return;
  }
  const auto min_n = field_integer(values, KEY_MIN_N);
  const auto max_n = field_integer(values, KEY_MAX_N);
  const auto value_min = field_integer(values, KEY_VALUE_MIN);
  const auto value_max = field_integer(values, KEY_VALUE_MAX);
  if (!min_n) {
    missing.emplace_back(KEY_MIN_N);
  }
  if (!max_n) {
    missing.emplace_back(KEY_MAX_N);
  }
  if (!value_min) {
    missing.emplace_back(KEY_VALUE_MIN);
  }
  if (!value_max) {
    missing.emplace_back(KEY_VALUE_MAX);
  }
  if (min_n && max_n && value_min && value_max) {
    bounds = Bounds{.min_n = *min_n, .max_n = *max_n, .value_min = *value_min, .value_max = *value_max};
  }
}

auto read_subtasks_json(const simdjson::dom::object& object, std::vector<SubtaskConstraint>& subtasks)
    -> Result<void> {
  auto entries = simdjson::dom::array{};
  if (object.at_key(KEY_SUBTASKS).get_array().get(entries) != simdjson::SUCCESS) {
    return std::unexpected(refuse("the stored specification carries no " + std::string{KEY_SUBTASKS} + " array"));
  }
  for (const auto entry : entries) {
    auto subtask = simdjson::dom::object{};
    if (entry.get_object().get(subtask) != simdjson::SUCCESS) {
      return std::unexpected(refuse("a stored subtask is not a JSON object"));
    }
    const auto position = field_integer(subtask, KEY_POSITION);
    const auto constraint = field_text(subtask, KEY_CONSTRAINT);
    if (!position || !constraint) {
      return std::unexpected(refuse("a stored subtask names no " + std::string{KEY_POSITION} + " or " +
                                    std::string{KEY_CONSTRAINT}));
    }
    subtasks.push_back(
        SubtaskConstraint{.position = static_cast<std::int32_t>(*position), .constraint = *constraint});
  }
  return {};
}


using artefact_id_tag = ::sqlpp::name_tag_of_t<cms::db::GenerationArtefacts_::Id>;
using inserted_row = ::sqlpp::result_row_t<::sqlpp::field_spec_t<artefact_id_tag, ::sqlpp::integral>>;

auto insert_artefact(cms::db::connection& database, std::int64_t job_id, std::int16_t step,
                     std::string_view kind, std::string_view object_key, std::string_view inline_text,
                     std::string_view digest, const ModelCall& call) -> std::int64_t {
  auto rows = ::sqlpp::result_t<cms::db::result, inserted_row>{database.query_text(
      std::string{RECORD_ARTEFACT},
      {job_id, static_cast<std::int64_t>(step), std::string{kind},
       object_key.empty() ? cms::db::bound_value{std::nullptr_t{}} : cms::db::bound_value{std::string{object_key}},
       inline_text.empty() ? cms::db::bound_value{std::nullptr_t{}}
                           : cms::db::bound_value{std::string{inline_text}},
       std::string{digest}, call.model_id, call.prompt_version, static_cast<std::int64_t>(call.seed),
       static_cast<std::int64_t>(call.call_index)})};
  if (rows.empty()) {
    throw std::runtime_error("the artefact row was written but its id could not be read back");
  }
  return rows.front().id;
}

}  // namespace

auto specification_json(const Specification& specification) -> std::string {
  return "{" + text_member(KEY_INPUT, specification.input) + "," + text_member(KEY_OUTPUT, specification.output) +
         "," + text_member(KEY_INPUT_SHAPE, N_THEN_VALUES) + "," +
         object_member(KEY_BOUNDS, bounds_json(specification.bounds)) + "," +
         object_member(KEY_SUBTASKS, subtasks_json(specification.subtasks)) + "}";
}

auto read_specification(std::string_view json) -> Result<Specification> {
  auto specification = Specification{};
  auto missing = std::vector<std::string>{};
  const auto used = detail::with_object(
      json, "the stored specification", [&](const simdjson::dom::object& object) -> Result<void> {
        const auto input = field_text(object, KEY_INPUT);
        const auto output = field_text(object, KEY_OUTPUT);
        const auto input_shape = field_text(object, KEY_INPUT_SHAPE);
        if (input) {
          specification.input = *input;
        } else {
          missing.emplace_back(KEY_INPUT);
        }
        if (output) {
          specification.output = *output;
        } else {
          missing.emplace_back(KEY_OUTPUT);
        }
        if (!input_shape) {
          missing.emplace_back(KEY_INPUT_SHAPE);
        } else if (*input_shape != N_THEN_VALUES) {
          return std::unexpected(refuse("the stored specification's " + std::string{KEY_INPUT_SHAPE} + " \"" +
                                        *input_shape + "\" is not one this pipeline builds: it builds " +
                                        std::string{N_THEN_VALUES}));
        }
        read_bounds_json(object, specification.bounds, missing);
        return read_subtasks_json(object, specification.subtasks);
      });
  if (!used) {
    return std::unexpected(used.error());
  }
  if (!missing.empty()) {
    return std::unexpected(refuse("the stored specification is missing " + listed(missing)));
  }
  return specification;
}

auto MemorySpecificationSink::record(std::int64_t job_id, const Step3Answer& answer,
                                     const OracleCheck& oracle) -> Result<std::string> {
  if (answer.answer_json.empty()) {
    return std::unexpected(Error::make(ErrorCode::invalid_argument, NO_ANSWER));
  }
  job_id_ = job_id;
  answer_ = answer;
  oracle_ = oracle;
  return cms::store::digest_of(answer.oracle_source);
}

SqlSpecificationSink::SqlSpecificationSink(::drogon::orm::DbClient& client, cms::store::ObjectStore& store)
    : client_(client), store_(store) {}

auto SqlSpecificationSink::record(std::int64_t job_id, const Step3Answer& answer,
                                  const OracleCheck& oracle) -> Result<std::string> {
  if (answer.answer_json.empty()) {
    return std::unexpected(Error::make(ErrorCode::invalid_argument, NO_ANSWER));
  }
  const auto smallest = sample_smallest(answer.specification);
  if (!smallest || *smallest != oracle.smallest_input) {
    return std::unexpected(Error::make(ErrorCode::invalid_argument,
                                       "the oracle was checked on a case the bounds do not name"));
  }
  try {
    const auto key = "generation/" + std::to_string(job_id) + "/step-3/oracle.cpp";
    const auto written = store_.write(key, answer.oracle_source);
    if (!written) {
      return std::unexpected(written.error());
    }
    auto transaction = cms::db::Transaction{client_};
    auto& database = transaction.connection();
    const auto answer_id = insert_artefact(database, job_id, PROOF_STEP, "specification", {}, answer.answer_json,
                                           cms::store::digest_of(answer.answer_json), answer.call);
    const auto oracle_id = insert_artefact(database, job_id, PROOF_STEP, "oracle", key, {}, *written, answer.call);
    database.execute_text(
        std::string{RECORD_SPECIFICATION},
        {job_id, specification_json(answer.specification), answer.limit.time_limit_ms, answer.limit.memory_limit_mb,
         answer.limit.derivation, answer_id, oracle_id});
    transaction.commit();
    const auto back = read_artefact(client_, store_, job_id, PROOF_STEP, "oracle");
    if (!back) {
      return std::unexpected(back.error());
    }
    if (back->digest != *written || back->bytes != answer.oracle_source) {
      return std::unexpected(Error::make(ErrorCode::conflict,
                                         "the oracle read back is not the program that was written: the row says " +
                                             back->digest + ", the write returned " + *written));
    }
    return *written;
  } catch (const std::exception& failure) {
    return std::unexpected(Error::make(ErrorCode::unavailable, failure.what()));
  }
}

}  // namespace cms::generate
