#include <cms/generate/candidate.hpp>

#include <cstdint>
#include <string>
#include <string_view>

#include "json.hpp"
#include "text.hpp"

namespace cms::generate {
namespace {

using detail::trimmed;

auto named(std::string& message, std::string_view name) -> void {
  if (!message.empty()) {
    message += ", ";
  }
  message += name;
}

auto read_candidate(const simdjson::dom::object& answer, CandidatePlan& candidate,
                    std::string_view what) -> Result<void> {
  auto wrong = std::string{};
  const auto approach = detail::field_text(answer, "approach");
  if (!approach || trimmed(*approach).empty()) {
    named(wrong, "approach");
  }
  const auto complexity = detail::field_text(answer, "intended_complexity");
  if (!complexity || trimmed(*complexity).empty()) {
    named(wrong, "intended_complexity");
  }
  const auto fits = detail::field_bool(answer, "fits_bounds");
  if (!fits) {
    named(wrong, "fits_bounds");
  }
  if (!wrong.empty()) {
    return std::unexpected(Error::make(
        ErrorCode::invalid_argument,
        std::string{what} +
            " must carry a non-blank approach, a non-blank intended_complexity and a boolean fits_bounds; this "
            "one is missing or has wrong: " +
            wrong));
  }
  candidate.approach = std::string{trimmed(*approach)};
  candidate.intended_complexity = std::string{trimmed(*complexity)};
  candidate.fits_bounds = *fits;
  candidate.plan_json = detail::minified(answer);
  return {};
}

}  // namespace

auto parse_candidate(const Completion& completion) -> Result<CandidatePlan> {
  auto candidate = CandidatePlan{.approach = {},
                                 .intended_complexity = {},
                                 .fits_bounds = false,
                                 .plan_json = {},
                                 .call = completion.call};
  const auto what = "the planner's answer for call " + std::to_string(completion.call.call_index + 1);
  const auto read = detail::with_object(completion.text, what, [&](const simdjson::dom::object& answer) {
    return read_candidate(answer, candidate, what);
  });
  if (!read) {
    return std::unexpected(read.error());
  }
  return candidate;
}

auto render_candidate(const CandidateRecord& record) -> std::string {
  auto block = "position " + std::to_string(record.position) + "\n";
  block += "approach: " + record.plan.approach + "\n";
  block += "intended complexity: " + record.plan.intended_complexity + "\n";
  block += std::string{"fits the declared bounds: "} + (record.plan.fits_bounds ? "yes" : "no") + "\n";
  block += "plan: " + record.plan.plan_json + "\n";
  return block;
}

}  // namespace cms::generate
