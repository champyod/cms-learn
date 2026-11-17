#include <cms/generate/specification.hpp>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <string>
#include <string_view>

#include <sqlpp23/sqlpp23.h>

#include <cms/db/connection.hpp>
#include <cms/db/tables.hpp>
#include <cms/db/transaction.hpp>
#include <cms/generate/artefact.hpp>
#include <cms/object_store.hpp>

namespace cms::generate {
namespace {

constexpr std::string_view NO_ANSWER = "step 3 recorded no answer";
constexpr std::int16_t PROOF_STEP = 3;

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

/// @return The specification the answer carries, as the JSON a reader reads beside the limits.
auto specification_json(const Step3Answer& answer) -> std::string {
  auto json = std::string{"{\"input\":\""};
  json += escaped(answer.specification.input);
  json += "\",\"output\":\"";
  json += escaped(answer.specification.output);
  json += "\",\"input_shape\":\"n_then_values\",\"bounds\":{\"min_n\":" +
          std::to_string(answer.specification.bounds.min_n) +
          ",\"max_n\":" + std::to_string(answer.specification.bounds.max_n) +
          ",\"value_min\":" + std::to_string(answer.specification.bounds.value_min) +
          ",\"value_max\":" + std::to_string(answer.specification.bounds.value_max) + "},\"subtasks\":[";
  for (std::size_t index = 0; index < answer.specification.subtasks.size(); ++index) {
    json += index == 0 ? "" : ",";
    json += "{\"position\":" + std::to_string(answer.specification.subtasks[index].position) +
            ",\"constraint\":\"" + escaped(answer.specification.subtasks[index].constraint) + "\"}";
  }
  json += "]}";
  return json;
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
        {job_id, specification_json(answer), answer.limit.time_limit_ms, answer.limit.memory_limit_mb,
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
