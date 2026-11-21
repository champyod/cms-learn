#pragma once

#include <cstdint>
#include <filesystem>
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
/// The half-limit margin ADR 0048 fixes: a reference must fit inside half of what the task allows.
inline constexpr std::int64_t REFERENCE_MARGIN_DIVISOR = 2;
inline constexpr std::int64_t KB_PER_MB = 1024;

/// @brief Kilobytes rounded up to whole megabytes, so a message never claims less than was measured.
[[nodiscard]] inline auto megabytes(std::int64_t kilobytes) -> std::int64_t {
  return (kilobytes + KB_PER_MB - 1) / KB_PER_MB;
}

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
  std::int64_t memory_used_limit_mb{0};
  std::int64_t memory_reserved_limit_mb{0};
};

/// @brief Re-runs the three methods ADR 0046 §4 makes certification: the reference pass, the brute-force
/// differential, and the limit measurement ADR 0048 fixes at half the declared limit.
///
/// Every method is attempted and reported by its own name, so one refusal carries the whole set (ADR 0049). The
/// cases are rebuilt from the generator rather than trusted from a row: the certificate states what ran, not what a
/// stored count claims.
[[nodiscard]] auto re_run_methods(const ReRunEvidence& evidence) -> std::vector<ReRunResult>;

/// @brief One artefact a certificate recorded: what it was, the row it was read from, and the digest it hashed to.
struct CertifiedArtefact {
  std::string role;
  std::int64_t artefact{0};
  std::string digest;
};

/// @brief The certificate step 7 writes: what it checked, the commit it checked, and the addresses it saw.
struct Certificate {
  std::int64_t job_id{0};
  std::string task_id;
  std::int32_t task_version{0};
  VerificationOutcome outcome{VerificationOutcome::pending};
  std::string commit_id;
  std::vector<std::string> checks;
  std::vector<CertifiedArtefact> artefacts;
};

/// @brief What the newest certificate for a task version still says (ADR 0046 §4).
struct CertificateState {
  /// False when the task version has no certificate row, so there is nothing to revalidate.
  bool is_certified{false};
  /// The outcome a reader may rely on: the recorded one while the evidence still matches, and `failed` when it does
  /// not, because a certificate whose artefacts moved certifies nothing.
  VerificationOutcome outcome{VerificationOutcome::pending};
  std::string commit_id;
  /// The recorded artefacts that no longer match, each named by its role with both digests; empty when it holds.
  std::vector<std::string> changed;
};

/// @brief Reads the newest certificate for a task version against the artefacts it certified (ADR 0046 §4).
///
/// The row records the digests it saw, so evidence cannot outlive the thing it proved. This is the reader for them:
/// every recorded artefact is looked up again, its row's digest is compared with the recorded one, and an
/// object-backed artefact's bytes are read back through that digest, so a row or a stored object that moved after the
/// run is named. When anything moved, the recorded outcome is ignored by name: a `failed` certificate is written
/// recording what changed and the task is moved off `verified`, so no reader can take the stale row for a
/// certification. Nothing is written when the certificate still holds or the task version has none.
///
/// The caller owns the transaction and must commit it for the repair to stand.
[[nodiscard]] auto revalidate_certificate(cms::db::connection& database, const cms::store::ObjectStore& store,
                                          std::string_view task_id, std::int32_t task_version)
    -> Result<CertificateState>;

/// @brief Step 7: re-runs the evidence of steps 3, 5 and 6 for @p job and writes the outcome.
///
/// Every artefact an earlier step cited is read back through its digest, the limits the specification derived are
/// compared with the task's, each subtask must still own a solution and a case set, and the specification is read
/// by the one reader its writer uses. The certificate written before this run is re-read at the point this run decides
/// an outcome (revalidate_certificate): one whose artefacts moved is recorded as failed and named in this
/// certificate's checks. The reference pass, the brute-force differential and the half-limit margin are
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

/// @brief Certifies a hand-written task by the checks it can carry (ADR 0057 §3).
///
/// The reference runs against every example the task ships and must match each expected answer; it must also fit inside
/// half its declared limits, the same margin a generated task's reference gets (ADR 0048). The differential does not run,
/// because a hand-written task ships no brute-force program to compare against, and the certificate's checks list says so
/// rather than leaving the gap silent.
///
/// A refusal names the file the reference came from and the example or measurement that failed, so an author knows which
/// solution to fix.
///
/// @param database The certifier's own connection: writing a certificate and moving a verification are its grants.
/// @param content_root The root the task's examples and its `task.json` are read from.
/// @param task_id The task to certify.
/// @param version The version to certify.
/// @param commit_id The commit the evidence is produced from, which the certificate records.
[[nodiscard]] auto certify_task(cms::db::connection& database, const std::filesystem::path& content_root,
                                std::string_view task_id, std::int32_t version, std::string_view commit_id)
    -> Result<Certificate>;

}  // namespace cms::generate
