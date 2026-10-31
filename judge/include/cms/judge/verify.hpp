#pragma once

#include <filesystem>

#include <cms/error.hpp>
#include <cms/judge/manifest.hpp>

namespace cms::judge {

/// @brief Runs a task's reference solution over every case and refuses a case it does not satisfy.
///
/// @param task_dir The task directory holding `examples/`.
/// @param reference The reference executable to run.
/// @return The manifest the cases amount to, or the first case whose output did not match.
///
/// The reference is what "verified" means here: a case whose expected output the reference does not
/// produce is a broken case, and no manifest is written for it. That is the whole point of the check —
/// a task whose examples disagree with its own solution must not reach a learner.
[[nodiscard]] Result<Manifest> verify_task(const std::filesystem::path& task_dir,
                                           const std::filesystem::path& reference);

}  // namespace cms::judge
