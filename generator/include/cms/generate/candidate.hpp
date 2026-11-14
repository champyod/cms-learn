#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <cms/error.hpp>
#include <cms/generate/model.hpp>

namespace cms::generate {

/// @brief One candidate plan: the three facts generation_candidates lifts into columns, plus the plan itself.
///
/// The plan body is the model's own JSON, minified, and is what `generation_candidates.plan` stores. Nothing in
/// steps 1 and 2 reads it field by field: the vocabulary a plan is written in belongs to the steps that
/// consume it, so this code refuses an answer that is not an object and invents nothing beyond the three
/// columns the schema already fixes.
struct CandidatePlan {
  std::string approach;
  std::string intended_complexity;
  bool fits_bounds{false};
  std::string plan_json;
  ModelCall call;
};

/// @brief One row of the plan stage's record: the candidate, the decision about it, and the call that made it.
///
/// The call facts travel here rather than in a column because generation_candidates holds none; provenance is
/// the object-store record's business (ADR 0023 and 0027), so the row carries them until a step writes them.
struct CandidateRecord {
  std::int32_t position{0};
  CandidatePlan plan;
  bool chosen{false};
  std::string reason;
};

/// @brief Reads one candidate out of the model's answer.
/// @param completion The answer, and the call facts that produced it.
/// @return The candidate, or one refusal naming every fact the answer is missing or has wrong: a blank
/// approach, a blank intended complexity, no boolean `fits_bounds`, or an answer that is not a JSON object.
[[nodiscard]] auto parse_candidate(const Completion& completion) -> Result<CandidatePlan>;

/// @brief Renders one candidate as the block step 2 reads: its approach, its cost, its fit and the plan itself.
[[nodiscard]] auto render_candidate(const CandidateRecord& record) -> std::string;

}  // namespace cms::generate
