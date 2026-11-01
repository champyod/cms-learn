#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <cms/db/connection.hpp>
#include <cms/error.hpp>

namespace cms::judge {

/// @brief How long a claim stays good without the worker that took it saying another word.
///
/// The lease is what makes a killed worker harmless. Its row returns to the queue on its own, so no
/// reconciliation step has to notice the death first, and the wait a learner sees is bounded by this number
/// rather than by an operator's attention.
inline constexpr std::chrono::minutes CLAIM_LEASE{5};

/// @brief A submission this worker now owns, as the claim statement returned it.
struct ClaimedSubmission {
  std::string id;
  std::string account_id;
  std::string task_id;
  std::int64_t task_version{0};
  std::string source;
};

/// @brief Claims the oldest unjudged submission no live worker holds.
///
/// @param database The connection the claim runs on.
/// @param worker_id The name recorded in `claimed_by`, so a human reading the row knows whose machine died.
/// @return The row this call took, or nothing when every unjudged row is either claimed and leased or gone.
///
/// The claim is one statement: the row is chosen and marked in the same breath, which is what stops two
/// workers from reading the same submission before either writes it. `SKIP LOCKED` makes a row another
/// worker is claiming right now invisible rather than something to wait for.
[[nodiscard]] Result<std::optional<ClaimedSubmission>> claim_submission(cms::db::connection& database,
                                                                       std::string_view worker_id);

/// @brief Clears every claim older than @p age, putting those rows back in the queue.
///
/// @param database The connection the release runs on.
/// @param age How long a claim must have stood to be considered abandoned.
/// @return How many rows were released.
///
/// The claim statement already takes a row whose lease ran out, so this is not what makes an abandoned row
/// run again. It is what makes `claimed_by` mean "alive": without it a stale name stays on the row, and an
/// operator reading the table cannot tell a worker that is mid-run from one that died an hour ago.
[[nodiscard]] Result<std::size_t> release_stale_claims(cms::db::connection& database,
                                                      std::chrono::minutes age);

}  // namespace cms::judge
