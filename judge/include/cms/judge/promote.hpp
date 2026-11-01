#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include <cms/error.hpp>

namespace cms::judge {

/// @brief What a promotion produced.
struct Promotion {
  /// The digest of the task's own files, which is also the artifact's name.
  std::string digest;
  /// Where the artifact was written.
  std::filesystem::path artifact;
};

/// @brief Publishes a draft, but only after the reference has been shown to satisfy every case.
///
/// @param task_dir The draft task directory.
/// @param artifact_dir Where the content-addressed artifact is written.
/// @param reference The reference executable the cases are checked against.
/// @return The digest and the artifact path, or the reason publication was refused.
///
/// Three refusals, in this order: a task whose `verification.status` is not `verified` is not published, a
/// task that is already published is not published again, and a task whose reference does not reproduce its
/// expected outputs is not published. The order matters — the cheapest refusal is the one that says the
/// machine evidence is missing, not the one that says a byte differed.
///
/// The artifact is named by the digest of the published bytes, so the same task published twice yields the
/// same path and two different tasks can never collide.
[[nodiscard]] Result<Promotion> promote_task(const std::filesystem::path& task_dir,
                                             const std::filesystem::path& artifact_dir,
                                             const std::filesystem::path& reference);

}  // namespace cms::judge
