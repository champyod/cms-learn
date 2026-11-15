#include <cms/generate/planner.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <cms/generate/data_frame.hpp>

#include "text.hpp"

namespace cms::generate {
namespace {

constexpr std::string_view ANSWER_SHAPE =
    "Answer with one JSON object and nothing else. It carries:\n"
    "  \"approach\": the algorithmic family this plan is built on, in a few words;\n"
    "  \"intended_complexity\": the asymptotic cost a correct solution needs, naming the variable it is in;\n"
    "  \"fits_bounds\": true when that cost fits the declared constraints, false when it cannot.\n"
    "Every other key of the object is the plan itself: the constraints, the subtasks and their intents, the "
    "limits you would derive, and the technique. Later steps read those keys, so name a fact you would defend.";

constexpr std::string_view ROLE =
    "You plan one competitive-programming task for a school judge. You do not write it.";

auto repeated_approach(const std::vector<CandidatePlan>& candidates) -> std::optional<std::string> {
  auto seen = std::vector<std::string>{};
  seen.reserve(candidates.size());
  for (const auto& candidate : candidates) {
    const auto folded = detail::normalised(candidate.approach);
    for (const auto& other : seen) {
      if (other == folded) {
        return candidate.approach;
      }
    }
    seen.push_back(folded);
  }
  return std::nullopt;
}

}  // namespace

auto planner_prompt(const Brief& brief, std::size_t shard, std::size_t count) -> Result<std::string> {
  const auto data = frame_data(ModelData{.label = "the teacher's brief", .text = render_brief(brief)});
  if (!data) {
    return std::unexpected(data.error());
  }
  auto prompt = std::string{ROLE};
  prompt += "\n\n";
  prompt += *data;
  prompt += "\nThis is candidate " + std::to_string(shard) + " of " + std::to_string(count) +
            ". The other proposals are written independently, so take the approach this brief suggests most "
            "strongly rather than a plan that agrees with one you cannot see.\n\n";
  prompt += ANSWER_SHAPE;
  return prompt;
}

auto plan_candidates(ModelClient& client, const Brief& brief, std::size_t count)
    -> Result<std::vector<CandidatePlan>> {
  if (count < MINIMUM_CANDIDATES) {
    return std::unexpected(Error::make(
        ErrorCode::invalid_argument,
        "step 1 asks for at least " + std::to_string(MINIMUM_CANDIDATES) + " candidate plans and was asked for " +
            std::to_string(count)));
  }
  const auto complete = refuse_incomplete_brief(brief);
  if (!complete) {
    return std::unexpected(complete.error());
  }

  auto candidates = std::vector<CandidatePlan>{};
  candidates.reserve(count);
  for (std::size_t shard = 1; shard <= count; ++shard) {
    const auto prompt = planner_prompt(brief, shard, count);
    if (!prompt) {
      return std::unexpected(prompt.error());
    }
    auto completion = client.complete(*prompt);
    if (!completion) {
      return std::unexpected(completion.error());
    }
    auto candidate = parse_candidate(*completion);
    if (!candidate) {
      return std::unexpected(candidate.error());
    }
    candidates.push_back(std::move(*candidate));
  }

  const auto repeated = repeated_approach(candidates);
  if (repeated) {
    return std::unexpected(Error::make(
        ErrorCode::invalid_argument,
        "two of the " + std::to_string(count) + " candidates name the same approach (\"" + *repeated +
            "\"); four candidates are four plans, and a plan written twice is not a second opinion"));
  }
  return candidates;
}

}  // namespace cms::generate
