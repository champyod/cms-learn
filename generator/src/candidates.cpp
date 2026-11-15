#include <cms/generate/candidates.hpp>

#include <cstdint>
#include <exception>
#include <string>
#include <stdexcept>
#include <string_view>
#include <vector>

#include <sqlpp23/sqlpp23.h>

#include <cms/db/connection.hpp>
#include <cms/db/tables.hpp>
#include <cms/db/transaction.hpp>
#include <cms/object_store.hpp>

namespace cms::generate {
namespace {

constexpr std::string_view NO_CANDIDATE = "a plan stage recorded no candidate";
constexpr std::int64_t PLANNER_STEP = 1;
constexpr std::int64_t JUDGE_STEP = 2;
constexpr std::string_view CANDIDATE_PLAN = "candidate-plan";
constexpr std::string_view JUDGEMENT = "judgement";

/// One row per model call (ADR 0030). Recording the same call again replaces the row it already wrote — a step
/// re-run must not leave two rows naming one call — while a second call at the same step, kind and locale is a
/// second row, which the unique key on (job, step, kind, locale, call_index) states. The id is returned because
/// the candidate that names this call has to carry it.
constexpr std::string_view RECORD_CALL = R"(
INSERT INTO generation_artefacts (job_id, step, kind, inline_text, digest, model_id, prompt_version, seed,
                                  call_index)
VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9)
ON CONFLICT (job_id, step, kind, locale, call_index) DO UPDATE
SET inline_text = excluded.inline_text, digest = excluded.digest, model_id = excluded.model_id,
    prompt_version = excluded.prompt_version, seed = excluded.seed
RETURNING id
)";

/// A decision is recorded once. The write names the conflict target and does nothing when a row is already there, so
/// a replay is answered as a conflict — the record already holds a decision at that position — instead of the unique
/// violation reaching the caller as a service that is temporarily down. A retry driver must be able to tell the two
/// apart: one says "a human should look", the other says "ask again".
constexpr std::string_view RECORD_DECISION = R"(
INSERT INTO generation_candidates (job_id, position, approach, intended_complexity, fits_bounds, plan, chosen,
                                   reason, call_artefact_id)
VALUES ($1, $2, $3, $4, $5, $6::jsonb, $7, $8, $9)
ON CONFLICT (job_id, position) DO NOTHING
)";

using artefact_id_tag = ::sqlpp::name_tag_of_t<cms::db::GenerationArtefacts_::Id>;
using inserted_row = ::sqlpp::result_row_t<::sqlpp::field_spec_t<artefact_id_tag, ::sqlpp::integral>>;

auto refused(std::string_view message) -> Error {
  return Error::make(ErrorCode::invalid_argument, message);
}

auto already_recorded(std::int64_t job_id, std::int32_t position) -> Error {
  return Error::make(ErrorCode::conflict, "generation job " + std::to_string(job_id) +
                                              " already records a decision at position " +
                                              std::to_string(position));
}

/// @return The id of the artefact row that names @p call, written with the answer it produced.
auto record_call(cms::db::connection& database, std::int64_t job_id, std::int64_t step,
                 std::string_view kind, std::string_view answer, const ModelCall& call)
    -> std::int64_t {
  auto rows = ::sqlpp::result_t<cms::db::result, inserted_row>{database.query_text(
      std::string{RECORD_CALL},
      {job_id, step, std::string{kind}, std::string{answer}, cms::store::digest_of(answer),
       call.model_id, call.prompt_version, static_cast<std::int64_t>(call.seed),
       static_cast<std::int64_t>(call.call_index)})};
  if (rows.empty()) {
    throw std::runtime_error("the call row was written but its id could not be read back");
  }
  return rows.front().id;
}

/// @return Whether this row is the one the write made; false when the decision at that position was already there.
auto insert_record(cms::db::connection& database, std::int64_t job_id, const CandidateRecord& record,
                   std::int64_t call_artefact_id) -> bool {
  const auto written = database.execute_text(
      std::string{RECORD_DECISION},
      {job_id, static_cast<std::int64_t>(record.position), record.plan.approach,
       record.plan.intended_complexity, record.plan.fits_bounds, record.plan.plan_json, record.chosen,
       record.reason, call_artefact_id});
  return written == 1;
}

}  // namespace

auto MemoryCandidateSink::record(std::int64_t job_id, const Judgement& judgement) -> Result<std::size_t> {
  if (judgement.records.empty()) {
    return std::unexpected(refused(NO_CANDIDATE));
  }
  job_id_ = job_id;
  judgement_ = judgement;
  calls_.clear();
  for (const auto& record : judgement.records) {
    calls_.push_back(record.plan.call);
  }
  calls_.push_back(judgement.call);
  return judgement_.records.size();
}

SqlCandidateSink::SqlCandidateSink(::drogon::orm::DbClient& client) : client_(client) {}

auto SqlCandidateSink::record(std::int64_t job_id, const Judgement& judgement)
    -> Result<std::size_t> {
  if (judgement.records.empty()) {
    return std::unexpected(refused(NO_CANDIDATE));
  }
  try {
    auto transaction = cms::db::Transaction{client_};
    auto& database = transaction.connection();
    for (const auto& record : judgement.records) {
      const auto call = record_call(database, job_id, PLANNER_STEP, CANDIDATE_PLAN,
                                    record.plan.plan_json, record.plan.call);
      if (!insert_record(database, job_id, record, call)) {
        return std::unexpected(already_recorded(job_id, record.position));
      }
    }
    // The judge's own call is recorded too: ADR 0030 asks for one row per call, and a decision with no call behind
    // it is the same gap the candidate's reference closes.
    record_call(database, job_id, JUDGE_STEP, JUDGEMENT, judgement.answer, judgement.call);
    transaction.commit();
    return judgement.records.size();
  } catch (const std::exception& failure) {
    return std::unexpected(Error::make(ErrorCode::unavailable, failure.what()));
  }
}

}  // namespace cms::generate
