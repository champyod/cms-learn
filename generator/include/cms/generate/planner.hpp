#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <cms/error.hpp>
#include <cms/generate/brief.hpp>
#include <cms/generate/candidate.hpp>
#include <cms/generate/model.hpp>

namespace cms::generate {

/// @brief The floor step 1 must clear: four candidate plans, not one answer holding four paragraphs (ADR 0015).
constexpr std::size_t MINIMUM_CANDIDATES = 4;

/// @brief Runs step 1: one model call per candidate, each its own shard, and no agreement between them.
///
/// A shard that produces no candidate ends the run with the call that failed, so a failure is one shard and
/// not a quieter average. Two candidates naming the same approach are refused as well: four restatements of
/// one plan are one candidate written four times, which ADR 0015 §2 forbids as a vote.
///
/// @param client The model the deployer configured, or a recording of it.
/// @param brief What the teacher asked for, as the authoring form collected it.
/// @param count How many candidates to ask for; anything below MINIMUM_CANDIDATES is refused.
/// @return The candidates in shard order, or the refusal that ended the step.
[[nodiscard]] auto plan_candidates(ModelClient& client, const Brief& brief,
                                   std::size_t count = MINIMUM_CANDIDATES)
    -> Result<std::vector<CandidatePlan>>;

/// @brief The prompt one shard of step 1 sends.
///
/// The teacher's brief travels inside a labelled data block, never pasted into the instruction: a brief is
/// content to plan from, and the boundary is what ADR 0029 makes of that sentence.
///
/// @param shard The 1-based shard this call owns.
/// @param count How many shards the run has, so the model knows it proposes one of several.
/// @return The prompt, or a refusal when the brief carries the frame's own markers.
[[nodiscard]] auto planner_prompt(const Brief& brief, std::size_t shard, std::size_t count)
    -> Result<std::string>;

}  // namespace cms::generate
