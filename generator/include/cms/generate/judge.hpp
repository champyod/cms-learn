#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <cms/error.hpp>
#include <cms/generate/candidate.hpp>
#include <cms/generate/model.hpp>

namespace cms::generate {

/// @brief Step 2's answer: one candidate chosen, and every other one kept beside the reason it lost.
struct Judgement {
  /// One record per candidate, in position order, with exactly one marked chosen.
  std::vector<CandidateRecord> records;
  ModelCall call;
};

/// @brief The candidate the judge chose.
/// @pre Exactly one record is marked chosen, which judge_candidates guarantees.
[[nodiscard]] auto chosen_of(const Judgement& judgement) -> const CandidateRecord&;

/// @brief Runs step 2: one call that names the chosen position and a reason for every other position.
///
/// A judge that drops a candidate, invents a position, explains no rejection or chooses a plan that does not
/// fit the declared bounds is refused: the record has to hold the choice and, next to it, why each alternative
/// lost, and a limit the task cannot honour is refused rather than widened (ADR 0014 §6, ADR 0015 step 2).
///
/// @param client The model the deployer configured, or a recording of it.
/// @param candidates Step 1's output, in position order.
/// @return The decision with one record per candidate, or the refusal that ended the step.
[[nodiscard]] auto judge_candidates(ModelClient& client, const std::vector<CandidatePlan>& candidates)
    -> Result<Judgement>;

/// @brief The prompt step 2 sends.
[[nodiscard]] auto judge_prompt(const std::vector<CandidatePlan>& candidates) -> std::string;

}  // namespace cms::generate
