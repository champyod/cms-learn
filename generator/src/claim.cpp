#include <cms/generate/claim.hpp>

#include <chrono>
#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <string_view>

#include <sqlpp23/sqlpp23.h>

#include <cms/db/connection.hpp>
#include <cms/db/tables.hpp>

namespace cms::generate {
namespace {

// One statement, because a claim assembled from a read and a write is two workers' chance to read the same row.
// `SKIP LOCKED` is the concurrency story: a row another worker is claiming right now is skipped rather than
// waited for, and the lock the subquery takes holds until this update has written the claim.
constexpr std::string_view CLAIM_SQL = R"claim(
UPDATE generation_jobs
SET claimed_by = $1, claimed_at = now(),
    lease_expires_at = now() + make_interval(secs => $2::int),
    state = 'running', attempts = attempts + 1, updated_at = now()
WHERE id = (SELECT id FROM generation_jobs
            WHERE state = 'queued'
               OR (state = 'running' AND lease_expires_at < now())
            ORDER BY created_at, id
            LIMIT 1
            FOR UPDATE SKIP LOCKED)
RETURNING id, task_id, task_version, attempts, claimed_by, lease_expires_at
)claim";

// The joint is the holder: a worker whose lease expired has no claim left to end, and the condition says so
// without a second read.
constexpr std::string_view FINISH_SQL = R"finish(
UPDATE generation_jobs
SET claimed_by = NULL, claimed_at = NULL, lease_expires_at = NULL,
    state = $2::generation_state, updated_at = now()
WHERE id = $1 AND claimed_by = $3
RETURNING id
)finish";

constexpr std::string_view ENDINGS = "awaiting-evidence, done or failed";

using claim_id_tag = ::sqlpp::name_tag_of_t<cms::db::GenerationJobs_::Id>;
using claim_task_id_tag = ::sqlpp::name_tag_of_t<cms::db::GenerationJobs_::TaskId>;
using claim_task_version_tag = ::sqlpp::name_tag_of_t<cms::db::GenerationJobs_::TaskVersion>;
using claim_attempts_tag = ::sqlpp::name_tag_of_t<cms::db::GenerationJobs_::Attempts>;
using claim_worker_tag = ::sqlpp::name_tag_of_t<cms::db::GenerationJobs_::ClaimedBy>;
using claim_lease_tag = ::sqlpp::name_tag_of_t<cms::db::GenerationJobs_::LeaseExpiresAt>;

using claim_row = ::sqlpp::result_row_t<
    ::sqlpp::field_spec_t<claim_id_tag, ::sqlpp::integral>,
    ::sqlpp::field_spec_t<claim_task_id_tag, ::sqlpp::text>,
    ::sqlpp::field_spec_t<claim_task_version_tag, ::sqlpp::integral>,
    ::sqlpp::field_spec_t<claim_attempts_tag, ::sqlpp::integral>,
    ::sqlpp::field_spec_t<claim_worker_tag, ::sqlpp::text>,
    ::sqlpp::field_spec_t<claim_lease_tag, ::sqlpp::timestamp>>;

using finished_row = ::sqlpp::result_row_t<::sqlpp::field_spec_t<claim_id_tag, ::sqlpp::integral>>;

auto claimed_from(const claim_row& row) -> JobClaim {
  return JobClaim{.id = row.id,
                  .task_id = std::string{row.taskId},
                  .task_version = static_cast<std::int32_t>(row.taskVersion),
                  .attempts = static_cast<std::int32_t>(row.attempts),
                  .worker = std::string{row.claimedBy},
                  .lease_expires_at = row.leaseExpiresAt};
}

auto is_ending(std::string_view outcome) -> bool {
  return outcome == "awaiting-evidence" || outcome == "done" || outcome == "failed";
}

}  // namespace

auto claim_job(cms::db::connection& database, std::string_view worker, std::chrono::seconds lease)
    -> Result<std::optional<JobClaim>> {
  if (worker.empty()) {
    return std::unexpected(
        Error::make(ErrorCode::invalid_argument, "a claim needs a worker name to write beside the moment"));
  }
  if (lease.count() <= 0) {
    return std::unexpected(
        Error::make(ErrorCode::invalid_argument, "a claim's lease must be a positive number of seconds"));
  }
  try {
    auto rows = ::sqlpp::result_t<cms::db::result, claim_row>{database.query_text(
        std::string{CLAIM_SQL}, {std::string{worker}, std::int64_t{lease.count()}})};
    if (rows.empty()) {
      return std::optional<JobClaim>{};
    }
    return std::optional<JobClaim>{claimed_from(rows.front())};
  } catch (const std::exception& failure) {
    return std::unexpected(Error::make(ErrorCode::unavailable, failure.what()));
  }
}

auto finish_claim(cms::db::connection& database, std::int64_t job_id, std::string_view worker,
                  std::string_view outcome) -> Result<void> {
  if (!is_ending(outcome)) {
    return std::unexpected(Error::make(ErrorCode::invalid_argument,
                                       "a running job ends in " + std::string{ENDINGS} + ", not \"" +
                                           std::string{outcome} + "\""));
  }
  try {
    auto rows = ::sqlpp::result_t<cms::db::result, finished_row>{database.query_text(
        std::string{FINISH_SQL}, {job_id, std::string{outcome}, std::string{worker}})};
    if (rows.empty()) {
      return std::unexpected(Error::make(ErrorCode::conflict,
                                         "no claim by \"" + std::string{worker} + "\" holds generation job " +
                                             std::to_string(job_id)));
    }
    return {};
  } catch (const std::exception& failure) {
    return std::unexpected(Error::make(ErrorCode::unavailable, failure.what()));
  }
}

}  // namespace cms::generate
