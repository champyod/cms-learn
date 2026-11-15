#include <cms/generate/candidates.hpp>

#include <cstdint>
#include <exception>
#include <string_view>
#include <vector>

#include <cms/db/connection.hpp>
#include <cms/db/transaction.hpp>

namespace cms::generate {
namespace {

constexpr std::string_view NO_CANDIDATE = "a plan stage recorded no candidate";

/// A decision is recorded once. The write names the conflict target and does nothing when a row is already there, so
/// a replay is answered as a conflict — the record already holds a decision at that position — instead of the unique
/// violation reaching the caller as a service that is temporarily down. A retry driver must be able to tell the two
/// apart: one says "a human should look", the other says "ask again".
constexpr std::string_view RECORD_DECISION = R"(
INSERT INTO generation_candidates (job_id, position, approach, intended_complexity, fits_bounds, plan, chosen, reason)
VALUES ($1, $2, $3, $4, $5, $6::jsonb, $7, $8)
ON CONFLICT (job_id, position) DO NOTHING
)";

auto refused(std::string_view message) -> Result<std::size_t> {
  return std::unexpected(Error::make(ErrorCode::invalid_argument, message));
}

auto already_recorded(std::int64_t job_id, std::int32_t position) -> Result<std::size_t> {
  return std::unexpected(Error::make(
      ErrorCode::conflict, "generation job " + std::to_string(job_id) +
                               " already records a decision at position " + std::to_string(position)));
}

/// @return Whether this row is the one the write made; false when the decision at that position was already there.
auto insert_record(cms::db::connection& database, std::int64_t job_id, const CandidateRecord& record)
    -> bool {
  const auto written = database.execute_text(
      std::string{RECORD_DECISION},
      {job_id, static_cast<std::int64_t>(record.position), record.plan.approach,
       record.plan.intended_complexity, record.plan.fits_bounds, record.plan.plan_json, record.chosen,
       record.reason});
  return written == 1;
}

}  // namespace

auto MemoryCandidateSink::record(std::int64_t job_id, const std::vector<CandidateRecord>& records)
    -> Result<std::size_t> {
  if (records.empty()) {
    return refused(NO_CANDIDATE);
  }
  job_id_ = job_id;
  rows_ = records;
  return rows_.size();
}

SqlCandidateSink::SqlCandidateSink(::drogon::orm::DbClient& client) : client_(client) {}

auto SqlCandidateSink::record(std::int64_t job_id, const std::vector<CandidateRecord>& records)
    -> Result<std::size_t> {
  if (records.empty()) {
    return refused(NO_CANDIDATE);
  }
  try {
    auto transaction = cms::db::Transaction{client_};
    auto& database = transaction.connection();
    for (const auto& record : records) {
      if (!insert_record(database, job_id, record)) {
        return already_recorded(job_id, record.position);
      }
    }
    transaction.commit();
    return records.size();
  } catch (const std::exception& failure) {
    return std::unexpected(Error::make(ErrorCode::unavailable, failure.what()));
  }
}

}  // namespace cms::generate
