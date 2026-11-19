#include <cms/generate/test_plan.hpp>

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
#include <cms/object_store.hpp>

namespace cms::generate {
namespace {

constexpr std::int16_t TEST_STEP = 6;

constexpr std::string_view RECORD_ARTEFACT = R"(
INSERT INTO generation_artefacts (job_id, step, kind, object_key, inline_text, digest, model_id, prompt_version,
                                  seed, call_index)
VALUES ($1, $2, $3, $4, NULL, $5, $6, $7, $8, $9)
ON CONFLICT (job_id, step, kind, locale, call_index) DO UPDATE
SET object_key = excluded.object_key, digest = excluded.digest, model_id = excluded.model_id,
    prompt_version = excluded.prompt_version, seed = excluded.seed
RETURNING id
)";

constexpr std::string_view RECORD_PROGRAM = R"(
INSERT INTO generation_programs (job_id, role, artefact_id, proof)
VALUES ($1, $2, $3, $4)
ON CONFLICT (job_id, role) DO UPDATE
SET artefact_id = excluded.artefact_id, proof = excluded.proof
)";

constexpr std::string_view RECORD_CASES = R"(
INSERT INTO generation_cases (job_id, subtask_position, artefact_id, case_count, proof)
VALUES ($1, $2, $3, $4, $5)
ON CONFLICT (job_id, subtask_position) DO UPDATE
SET artefact_id = excluded.artefact_id, case_count = excluded.case_count, proof = excluded.proof
)";

using artefact_id_tag = ::sqlpp::name_tag_of_t<cms::db::GenerationArtefacts_::Id>;
using inserted_row = ::sqlpp::result_row_t<::sqlpp::field_spec_t<artefact_id_tag, ::sqlpp::integral>>;

auto insert_artefact(cms::db::connection& database, std::int64_t job_id, std::string_view key,
                     std::string_view digest, std::string_view kind, const ModelCall& call) -> std::int64_t {
  auto rows = ::sqlpp::result_t<cms::db::result, inserted_row>{database.query_text(
      std::string{RECORD_ARTEFACT},
      {job_id, static_cast<std::int64_t>(TEST_STEP), std::string{kind}, std::string{key}, std::string{digest},
       call.model_id, call.prompt_version, static_cast<std::int64_t>(call.seed),
       static_cast<std::int64_t>(call.call_index)})};
  if (rows.empty()) {
    throw std::runtime_error("the step-6 artefact row was written but its id could not be read back");
  }
  return rows.front().id;
}

auto cases_json(const std::vector<GeneratedCase>& cases, std::int32_t position) -> std::string {
  auto json = std::string{"["};
  auto first = true;
  for (const auto& sample : cases) {
    if (sample.subtask_position != position) {
      continue;
    }
    if (!first) {
      json += ',';
    }
    first = false;
    json += "{\"seed\":" + std::to_string(sample.seed) + ",\"n\":" + std::to_string(sample.n) + "}";
  }
  json += ']';
  return json;
}

}  // namespace

SqlTestPlanSink::SqlTestPlanSink(::drogon::orm::DbClient& client, cms::store::ObjectStore& store)
    : client_(client), store_(store) {}

auto SqlTestPlanSink::record(std::int64_t job_id, const Step6Answer& answer,
                             const std::vector<Score>& scores) -> Result<std::size_t> {
  (void)scores;
  if (answer.generator.source.empty() || answer.checker.source.empty()) {
    return std::unexpected(Error::make(ErrorCode::invalid_argument,
                                       "step 6 recorded no generator or no checker"));
  }
  auto written = std::size_t{0};
  const auto generator_key = "generation/" + std::to_string(job_id) + "/step-6/generator.cpp";
  const auto checker_key = "generation/" + std::to_string(job_id) + "/step-6/checker.cpp";
  const auto generator_digest = store_.write(generator_key, answer.generator.source);
  if (!generator_digest) {
    return std::unexpected(generator_digest.error());
  }
  const auto checker_digest = store_.write(checker_key, answer.checker.source);
  if (!checker_digest) {
    return std::unexpected(checker_digest.error());
  }
  auto positions = std::vector<std::int32_t>{};
  for (const auto& sample : answer.cases) {
    if (positions.empty() || positions.back() != sample.subtask_position) {
      positions.push_back(sample.subtask_position);
    }
  }
  auto case_digests = std::vector<std::pair<std::int32_t, std::string>>{};
  for (const auto position : positions) {
    const auto key = "generation/" + std::to_string(job_id) + "/step-6/cases-" + std::to_string(position) + ".json";
    const auto digest = store_.write(key, cases_json(answer.cases, position));
    if (!digest) {
      return std::unexpected(digest.error());
    }
    case_digests.emplace_back(position, *digest);
  }
  try {
    auto transaction = cms::db::Transaction{client_};
    auto& database = transaction.connection();
    const auto generator_id = insert_artefact(database, job_id, generator_key, *generator_digest, "generator",
                                              answer.generator.call);
    database.execute_text(std::string{RECORD_PROGRAM},
                          {job_id, "generator", generator_id,
                           "the same seed twice prints the same bytes, and every case matches the bounds"});
    ++written;
    const auto checker_id =
        insert_artefact(database, job_id, checker_key, *checker_digest, "checker", answer.checker.call);
    database.execute_text(std::string{RECORD_PROGRAM},
                          {job_id, "checker", checker_id,
                           "accepts the expected answer and rejects a planted wrong one"});
    ++written;
    for (const auto& [position, digest] : case_digests) {
      const auto count = std::count_if(answer.cases.begin(), answer.cases.end(), [&](const GeneratedCase& sample) {
        return sample.subtask_position == position;
      });
      const auto key = "generation/" + std::to_string(job_id) + "/step-6/cases-" + std::to_string(position) + ".json";
      const auto id = insert_artefact(database, job_id, key, digest, "cases", answer.generator.call);
      database.execute_text(std::string{RECORD_CASES},
                            {job_id, position, id, static_cast<std::int64_t>(count),
                             std::to_string(count) + " case(s) generated for subtask " + std::to_string(position)});
      ++written;
    }
    transaction.commit();
    return written;
  } catch (const std::exception& failure) {
    return std::unexpected(Error::make(ErrorCode::unavailable, failure.what()));
  }
}

}  // namespace cms::generate
