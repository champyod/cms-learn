#include <cms/generate/solution.hpp>

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

constexpr std::int16_t SOLUTION_STEP = 5;
constexpr std::string_view REFERENCE_KIND = "reference";
constexpr std::string_view PARTIAL_KIND = "subtask-solution";
constexpr std::string_view NO_REFERENCE = "step 5 recorded no reference solution";

constexpr std::string_view RECORD_ARTEFACT = R"(
INSERT INTO generation_artefacts (job_id, step, kind, object_key, inline_text, digest, model_id, prompt_version,
                                  seed, call_index)
VALUES ($1, $2, $3, $4, NULL, $5, $6, $7, $8, $9)
ON CONFLICT (job_id, step, kind, locale, call_index) DO UPDATE
SET object_key = excluded.object_key, digest = excluded.digest, model_id = excluded.model_id,
    prompt_version = excluded.prompt_version, seed = excluded.seed
RETURNING id
)";

constexpr std::string_view RECORD_SOLUTION = R"(
INSERT INTO generation_solutions (job_id, position, artefact_id, proof)
VALUES ($1, $2, $3, $4)
ON CONFLICT (job_id, position) DO UPDATE
SET artefact_id = excluded.artefact_id, proof = excluded.proof
)";

using artefact_id_tag = ::sqlpp::name_tag_of_t<cms::db::GenerationArtefacts_::Id>;
using inserted_row = ::sqlpp::result_row_t<::sqlpp::field_spec_t<artefact_id_tag, ::sqlpp::integral>>;

auto insert_artefact(cms::db::connection& database, std::int64_t job_id, std::string_view kind,
                     std::string_view object_key, std::string_view digest, const ModelCall& call)
    -> std::int64_t {
  auto rows = ::sqlpp::result_t<cms::db::result, inserted_row>{database.query_text(
      std::string{RECORD_ARTEFACT},
      {job_id, static_cast<std::int64_t>(SOLUTION_STEP), std::string{kind}, std::string{object_key},
       std::string{digest}, call.model_id, call.prompt_version, static_cast<std::int64_t>(call.seed),
       static_cast<std::int64_t>(call.call_index)})};
  if (rows.empty()) {
    throw std::runtime_error("the solution row was written but its id could not be read back");
  }
  return rows.front().id;
}

auto oracle_key(std::int64_t job_id, std::int32_t position) -> std::string {
  return "generation/" + std::to_string(job_id) + "/step-5/" +
         (position == 0 ? "reference" : "subtask-" + std::to_string(position)) + ".cpp";
}

}  // namespace

SqlSolutionSink::SqlSolutionSink(::drogon::orm::DbClient& client, cms::store::ObjectStore& store)
    : client_(client), store_(store) {}

auto SqlSolutionSink::record(std::int64_t job_id, const Step5Answer& answer,
                             const std::vector<PartialProof>& proofs) -> Result<std::size_t> {
  if (answer.reference.source.empty()) {
    return std::unexpected(Error::make(ErrorCode::invalid_argument, NO_REFERENCE));
  }
  if (proofs.size() != answer.partials.size()) {
    return std::unexpected(Error::make(
        ErrorCode::invalid_argument,
        "step 5 has " + std::to_string(answer.partials.size()) + " partial solution(s) and " +
            std::to_string(proofs.size()) + " proof(s); each solution needs the proof it earned"));
  }
  auto written = std::vector<std::pair<std::int32_t, std::string>>{};
  const auto reference = store_.write(oracle_key(job_id, 0), answer.reference.source);
  if (!reference) {
    return std::unexpected(reference.error());
  }
  written.emplace_back(0, *reference);
  for (const auto& partial : answer.partials) {
    const auto digest = store_.write(oracle_key(job_id, partial.position), partial.source);
    if (!digest) {
      return std::unexpected(digest.error());
    }
    written.emplace_back(partial.position, *digest);
  }
  try {
    auto transaction = cms::db::Transaction{client_};
    auto& database = transaction.connection();
    const auto reference_id = insert_artefact(database, job_id, REFERENCE_KIND, oracle_key(job_id, 0),
                                              written.front().second, answer.reference.call);
    const auto reference_proof = "the reference agrees with the oracle on the smallest cases the bounds admit";
    database.execute_text(std::string{RECORD_SOLUTION}, {job_id, 0, reference_id, reference_proof});
    for (std::size_t index = 0; index < answer.partials.size(); ++index) {
      const auto& partial = answer.partials[index];
      const auto& proof = proofs[index];
      const auto id = insert_artefact(database, job_id, PARTIAL_KIND, oracle_key(job_id, partial.position),
                                      written[index + 1].second, partial.call);
      database.execute_text(std::string{RECORD_SOLUTION},
                            {job_id, partial.position, id, proof.sentence});
    }
    transaction.commit();
    return written.size();
  } catch (const std::exception& failure) {
    return std::unexpected(Error::make(ErrorCode::unavailable, failure.what()));
  }
}

}  // namespace cms::generate
