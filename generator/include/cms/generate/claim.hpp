#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

#include <cms/db/connection.hpp>
#include <cms/error.hpp>

namespace cms::generate {

/// How long one claim lasts. A worker that dies holds its job only until this deadline, so the number is the
/// bound on how long a queue can look busy while nothing is working it.
constexpr auto CLAIM_LEASE = std::chrono::seconds{600};

/// @brief One generation job a worker has taken, and the deadline it holds it until.
struct JobClaim {
  std::int64_t id{0};
  std::string task_id;
  std::int32_t task_version{0};
  /// The attempt this claim is, counted from one: the first worker to take a job makes it 1, a worker that took
  /// it over from an expired lease makes it 2, and a record that says "attempt 3" is a job three workers tried.
  std::int32_t attempts{0};
  std::string worker;
  cms::db::timestamp lease_expires_at{};
};

/// @brief Takes the oldest queued job, or a running one whose lease expired, in one statement.
///
/// The statement is one UPDATE whose subquery locks exactly one row with `FOR UPDATE SKIP LOCKED`: a row another
/// worker is claiming at this instant is not a row this one waits for and not a row it can be handed twice,
/// because the lock holds until the update that named it has written the claim (ADR 0015).
///
/// @param database The worker's connection.
/// @param worker The name a person reads after a worker dies without clearing its claim.
/// @param lease How long the claim lasts before another worker may take the job.
/// @return The claim, or nothing when the queue holds nothing claimable.
[[nodiscard]] auto claim_job(cms::db::connection& database, std::string_view worker,
                             std::chrono::seconds lease = CLAIM_LEASE)
    -> Result<std::optional<JobClaim>>;

/// @brief Ends an attempt by its holder: the claim is cleared and the job moves to @p outcome.
///
/// @param job_id The row the claim was taken on.
/// @param worker The holder, so a worker that lost its lease cannot end another's attempt.
/// @param outcome Where the job goes: `awaiting-evidence`, `done` or `failed`, the only endings the record
/// allows from a running job.
/// @return Nothing, or a refusal naming the job when no claim held by @p worker exists.
[[nodiscard]] auto finish_claim(cms::db::connection& database, std::int64_t job_id,
                                std::string_view worker, std::string_view outcome) -> Result<void>;

}  // namespace cms::generate
