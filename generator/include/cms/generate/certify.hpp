#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <cms/db/connection.hpp>
#include <cms/error.hpp>
#include <cms/generate/specification.hpp>
#include <cms/object_store.hpp>

namespace cms::generate {

/// @brief What a certification decided. `pending` is the state a task keeps while nothing has certified it.
enum class VerificationOutcome : std::uint8_t { pending, verified, failed };

/// @return The name the database records for @p outcome.
[[nodiscard]] auto outcome_name(VerificationOutcome outcome) -> std::string_view;

/// @brief One method the certifier re-ran: whether its evidence held, and the sentence the certificate records.
struct ReRunResult {
  bool is_passed{false};
  std::string sentence;
};

/// @brief The evidence the re-runs read: step 3's specification, steps 5 and 6's programs, and the declared limits.
struct ReRunEvidence {
  Specification specification;
  std::string generator_source;
  std::string checker_source;
  std::string oracle_source;
  std::string reference_source;
  std::vector<std::pair<std::int32_t, std::string>> partial_sources;
  /// One entry per subtask: its position, and how many cases step 6 recorded for it.
  std::vector<std::pair<std::int32_t, std::int64_t>> recorded_case_counts;
  std::int64_t time_limit_ms{0};
  std::int64_t memory_limit_mb{0};
};

/// @brief Re-runs the three methods ADR 0046 §4 makes certification: the reference pass, the brute-force
/// differential, and the limit measurement ADR 0048 fixes at half the declared limit.
///
/// Every method is attempted and reported by its own name, so one refusal carries the whole set (ADR 0049). The
/// cases are rebuilt from the generator rather than trusted from a row: the certificate states what ran, not what a
/// stored count claims.
[[nodiscard]] auto re_run_methods(const ReRunEvidence& evidence) -> std::vector<ReRunResult>;

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
/// compared with the task's, each subtask must still own a solution and a case set, and the specification is read
/// by the one reader its writer uses. The reference pass, the brute-force differential and the half-limit margin are
/// then re-run as ADR 0046 §4 requires. Incomplete or stale evidence is refused by name and nothing is written;
/// evidence that reads back but whose re-run fails is written as `failed`, naming every check that failed.
///
/// The caller owns the transaction: the certificate row and the task's verification move together or not at all.
/// @param database The certifier's connection; the role behind it is cmslearn_certifier, not the generator's.
/// @param store The client every cited artefact is read back through.
/// @param job_id The job whose evidence is certified.
/// @param commit_id The commit the evidence was produced from, recorded so a later reader knows what was checked.
[[nodiscard]] auto certify_job(cms::db::connection& database, const cms::store::ObjectStore& store,
                               std::int64_t job_id, std::string_view commit_id) -> Result<Certificate>;

}  // namespace cms::generate
