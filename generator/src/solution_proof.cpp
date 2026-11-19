#include <cms/generate/solution.hpp>

#include <algorithm>
#include <cstdint>
#include <regex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cms::generate {
namespace {

auto refuse(std::string_view message) -> Error {
  return Error::make(ErrorCode::invalid_argument, message);
}

auto input_for(const Bounds& bounds, std::int64_t count) -> std::string {
  auto input = std::to_string(count) + "\n";
  for (std::int64_t index = 0; index < count; ++index) {
    input += std::to_string(bounds.value_min);
    input += index + 1 == count ? '\n' : ' ';
  }
  return input;
}

auto trimmed(std::string_view text) -> std::string_view {
  const auto first = text.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) {
    return {};
  }
  const auto last = text.find_last_not_of(" \t\r\n");
  return text.substr(first, last - first + 1);
}

}  // namespace

auto subtask_bound(std::string_view constraint) -> Result<std::int64_t> {
  const auto pattern = std::regex{R"((?:<|≤|<=)\s*=?\s*([0-9]{1,18}))"};
  auto match = std::smatch{};
  const auto text = std::string{constraint};
  if (!std::regex_search(text, match, pattern)) {
    return std::unexpected(Error::make(
        ErrorCode::invalid_argument,
        "the constraint \"" + text + "\" names no bound, so no case can be shown to lie outside the subtask"));
  }
  return std::stoll(match[1].str());
}

auto admissible_cases(const Specification& specification, std::int64_t outside_n) -> Result<std::vector<SmallCase>> {
  auto cases = std::vector<SmallCase>{};
  cases.push_back(SmallCase{.input = input_for(specification.bounds, specification.bounds.min_n),
                            .n = specification.bounds.min_n});
  if (outside_n > specification.bounds.max_n) {
    return std::unexpected(Error::make(
        ErrorCode::invalid_argument,
        "the subtask's bound " + std::to_string(outside_n) + " is above the task's own bound " +
            std::to_string(specification.bounds.max_n) +
            ", so there is no case where the subtask stops and the task continues"));
  }
  if (outside_n > specification.bounds.min_n) {
    cases.push_back(SmallCase{.input = input_for(specification.bounds, outside_n), .n = outside_n});
  }
  return cases;
}

auto subtask_cases(const Specification& specification, std::int64_t bound) -> SubtaskCases {
  const auto inside = std::min(bound, specification.bounds.max_n);
  auto cases = SubtaskCases{};
  cases.inside.push_back(SmallCase{.input = input_for(specification.bounds, specification.bounds.min_n),
                                   .n = specification.bounds.min_n});
  if (inside > specification.bounds.min_n) {
    cases.inside.push_back(SmallCase{.input = input_for(specification.bounds, inside), .n = inside});
  }
  if (inside < specification.bounds.max_n) {
    cases.outside.push_back(SmallCase{.input = input_for(specification.bounds, inside + 1), .n = inside + 1});
  }
  return cases;
}

auto check_solutions(const Step5Answer& answer, const Specification& specification,
                     std::string_view oracle_source, const std::vector<SmallCase>& cases,
                     std::int64_t time_limit_ms) -> Result<std::vector<PartialProof>> {
  if (cases.empty()) {
    return std::unexpected(refuse("the bounds admit no case to check a solution against"));
  }
  for (const auto& sample : cases) {
    const auto oracle = run_program(oracle_source, sample.input, time_limit_ms);
    if (!oracle) {
      return std::unexpected(Error::make(ErrorCode::invalid_argument,
                                         "the oracle " + oracle.error().message + " (n = " + std::to_string(sample.n) + ")"));
    }
    const auto reference = run_program(answer.reference.source, sample.input, time_limit_ms);
    if (!reference) {
      return std::unexpected(Error::make(ErrorCode::invalid_argument,
                                         "the reference solution " + reference.error().message + " (n = " +
                                             std::to_string(sample.n) + ")"));
    }
    if (trimmed(*reference) != trimmed(*oracle)) {
      return std::unexpected(Error::make(
          ErrorCode::invalid_argument,
          "the reference solution disagrees with the oracle on the smallest case (n = " +
              std::to_string(sample.n) + "): the oracle answered \"" + std::string{trimmed(*oracle)} +
              "\", the reference answered \"" + std::string{trimmed(*reference)} + "\""));
    }
  }
  auto proofs = std::vector<PartialProof>{};
  for (const auto& partial : answer.partials) {
    const auto constraint = std::find_if(
        specification.subtasks.begin(), specification.subtasks.end(),
        [&](const SubtaskConstraint& subtask) { return subtask.position == partial.position; });
    if (constraint == specification.subtasks.end()) {
      return std::unexpected(refuse("the answer carries a solution for subtask " +
                                    std::to_string(partial.position) + ", which the specification does not name"));
    }
    const auto bound = subtask_bound(constraint->constraint);
    if (!bound) {
      return std::unexpected(Error::make(ErrorCode::invalid_argument,
                                         "the solution for subtask " + std::to_string(partial.position) + ": " +
                                             bound.error().message));
    }
    const auto owned = subtask_cases(specification, *bound);
    auto proof = PartialProof{.position = partial.position, .inside_cases = 0, .failed_outside = false, .sentence = {}};
    for (const auto& sample : owned.inside) {
      const auto oracle = run_program(oracle_source, sample.input, time_limit_ms);
      const auto ran = run_program(partial.source, sample.input, time_limit_ms);
      if (!ran || !oracle || trimmed(*ran) != trimmed(*oracle)) {
        return std::unexpected(Error::make(
            ErrorCode::invalid_argument,
            "the solution for subtask " + std::to_string(partial.position) + " does not pass its own subtask " +
                (ran ? "(n = " + std::to_string(sample.n) + ")" : std::string{ran.error().message})));
      }
      ++proof.inside_cases;
    }
    for (const auto& sample : owned.outside) {
      const auto oracle = run_program(oracle_source, sample.input, time_limit_ms);
      const auto ran = run_program(partial.source, sample.input, time_limit_ms);
      if (!ran || !oracle || trimmed(*ran) != trimmed(*oracle)) {
        proof.failed_outside = true;
        break;
      }
    }
    if (!proof.failed_outside) {
      return std::unexpected(Error::make(
          ErrorCode::invalid_argument,
          "the solution for subtask " + std::to_string(partial.position) +
              " passes the case outside its subtask, so it is not a partial: a weaker solution has to lose the "
              "points it does not earn"));
    }
    proof.sentence = "subtask " + std::to_string(partial.position) + ": passed " +
                     std::to_string(proof.inside_cases) + " case(s) inside its bound and failed a case outside it";
    proofs.push_back(std::move(proof));
  }
  for (const auto& subtask : specification.subtasks) {
    const auto found = std::find_if(proofs.begin(), proofs.end(),
                                    [&](const PartialProof& proof) { return proof.position == subtask.position; });
    if (found == proofs.end()) {
      return std::unexpected(refuse("subtask " + std::to_string(subtask.position) +
                                    " has no solution, so its points rest on a claim nobody measured"));
    }
  }
  return proofs;
}

}  // namespace cms::generate
