#include <cms/generate/candidates.hpp>

#include <cstdint>
#include <exception>
#include <string_view>
#include <vector>

#include <sqlpp23/sqlpp23.h>

#include <cms/db/connection.hpp>
#include <cms/db/tables.hpp>
#include <cms/db/transaction.hpp>

namespace cms::generate {
namespace {

constexpr std::string_view NO_CANDIDATE = "a plan stage recorded no candidate";

auto refused(std::string_view message) -> Result<std::size_t> {
  return std::unexpected(Error::make(ErrorCode::invalid_argument, message));
}

auto insert_record(cms::db::connection& database, std::int64_t job_id,
                   const CandidateRecord& record) -> void {
  database(sqlpp::insert_into(cms::db::generationCandidates)
               .set(cms::db::generationCandidates.jobId = job_id,
                    cms::db::generationCandidates.position = static_cast<std::int64_t>(record.position),
                    cms::db::generationCandidates.approach = record.plan.approach,
                    cms::db::generationCandidates.intendedComplexity = record.plan.intended_complexity,
                    cms::db::generationCandidates.fitsBounds = record.plan.fits_bounds,
                    cms::db::generationCandidates.plan = record.plan.plan_json,
                    cms::db::generationCandidates.chosen = record.chosen,
                    cms::db::generationCandidates.reason = record.reason));
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
      insert_record(database, job_id, record);
    }
    transaction.commit();
    return records.size();
  } catch (const std::exception& failure) {
    return std::unexpected(Error::make(ErrorCode::unavailable, failure.what()));
  }
}

}  // namespace cms::generate
