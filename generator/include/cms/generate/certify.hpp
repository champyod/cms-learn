#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <cms/db/connection.hpp>
#include <cms/error.hpp>
#include <cms/object_store.hpp>

namespace cms::generate {

/// @brief What a certification decided. `pending` is the state a task keeps while nothing has certified it.
enum class VerificationOutcome : std::uint8_t { pending, verified, failed };

/// @return The name the database records for @p outcome.
[[nodiscard]] auto outcome_name(VerificationOutcome outcome) -> std::string_view;

/// @brief The certificate step 7 writes: what it checked, the commit it checked, and the addresses it saw.
struct Certificate {
  std::int64_t job_id{0};
  std::string task_id;
  std::int32_t task_version{0};
  VerificationOutcome outcome{VerificationOutcome::pending};
  std::string commit_id;
  std::vector<std::string> checks;
  std::vector<std::string> digests;
};

/// @brief Step 7: re-runs the evidence of steps 3, 5 and 6 for @p job and writes the outcome.
///
/// Every artefact an earlier step cited is read back through its digest, the limits the specification derived are
/// compared with the task's, each subtask must still own a solution and a case set, and the oracle and the reference
/// are compiled and run on the smallest case the bounds admit. Incomplete or stale evidence is refused by name and
/// nothing is written; evidence that reads back but whose re-run disagrees is written as `failed`.
///
/// The caller owns the transaction: the certificate row and the task's verification move together or not at all.
/// @param database The certifier's connection; the role behind it is cmslearn_certifier, not the generator's.
/// @param store The client every cited artefact is read back through.
/// @param job_id The job whose evidence is certified.
/// @param commit_id The commit the evidence was produced from, recorded so a later reader knows what was checked.
[[nodiscard]] auto certify_job(cms::db::connection& database, const cms::store::ObjectStore& store,
                               std::int64_t job_id, std::string_view commit_id) -> Result<Certificate>;

}  // namespace cms::generate
