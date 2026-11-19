#include <cms/generate/solution.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <cms/generate/sampler.hpp>
#include <cms/tokens.hpp>

namespace cms::generate {
namespace {

auto refuse(std::string_view message) -> Error {
  return Error::make(ErrorCode::invalid_argument, message);
}

/// @return @p text with its whitespace runs collapsed, so a refusal quotes an answer on one line, never compares it.
auto quoted(std::string_view text) -> std::string {
  auto out = std::string{};
  auto pending_space = false;
  for (const auto character : text) {
    if (character == ' ' || character == '\t' || character == '\n' || character == '\r') {
      pending_space = !out.empty();
      continue;
    }
    if (pending_space) {
      out += ' ';
      pending_space = false;
    }
    out += character;
  }
  return out;
}

auto sampled(const Specification& specification, const cms::SubtaskBounds& narrowing, std::int64_t count,
             Placement placement) -> Result<std::vector<SmallCase>> {
  const auto samples = sample_cases(SampleRequest{.shape = N_THEN_VALUES,
                                                  .bounds = specification.bounds,
                                                  .narrowing = narrowing,
                                                  .seed = CASE_SEED,
                                                  .count = count,
                                                  .placement = placement});
  if (!samples) {
    return std::unexpected(samples.error());
  }
  auto cases = std::vector<SmallCase>{};
  cases.reserve(samples->size());
  for (const auto& sample : *samples) {
    cases.push_back(SmallCase{.input = sample.input, .n = sample.n});
  }
  return cases;
}

/// @return A narrowing admitting one case outside @p group but inside @p task, or a refusal when the group admits
/// every case the task does.
auto outside_narrowing(const cms::SubtaskBounds& group, const cms::TaskBounds& task)
    -> Result<cms::SubtaskBounds> {
  const auto admitted = cms::narrowed(group, task);
  if (admitted.min_n > task.min_n) {
    return cms::SubtaskBounds{.max_n = task.min_n};
  }
  if (admitted.max_n < task.max_n) {
    return cms::SubtaskBounds{.min_n = admitted.max_n + 1};
  }
  if (admitted.value_max < task.value_max) {
    return cms::SubtaskBounds{.value_min = admitted.value_max + 1};
  }
  if (admitted.value_min > task.value_min) {
    return cms::SubtaskBounds{.value_max = admitted.value_min - 1};
  }
  return std::unexpected(
      refuse("the subtask admits every case the task admits, so no case can be shown to lie outside it"));
}

}  // namespace

auto admissible_cases(const Specification& specification, const SubtaskConstraint& subtask)
    -> Result<std::vector<SmallCase>> {
  return sampled(specification, subtask.bounds, GROUP_CASES, Placement::spread);
}

auto subtask_cases(const Specification& specification, const SubtaskConstraint& subtask)
    -> Result<SubtaskCases> {
  auto cases = SubtaskCases{};
  const auto inside = sampled(specification, subtask.bounds, GROUP_CASES, Placement::spread);
  if (!inside) {
    return std::unexpected(inside.error());
  }
  cases.inside = *inside;
  const auto outside = outside_narrowing(subtask.bounds, specification.bounds);
  if (!outside) {
    return std::unexpected(outside.error());
  }
  const auto beyond = sampled(specification, *outside, 1, Placement::smallest);
  if (!beyond) {
    return std::unexpected(beyond.error());
  }
  cases.outside = *beyond;
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
    if (!cms::tokens_equal(*oracle, *reference)) {
      return std::unexpected(Error::make(
          ErrorCode::invalid_argument,
          "the reference solution disagrees with the oracle at n = " + std::to_string(sample.n) +
              ": the oracle answered \"" + quoted(*oracle) + "\", the reference answered \"" +
              quoted(*reference) + "\""));
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
    const auto owned = subtask_cases(specification, *constraint);
    if (!owned) {
      return std::unexpected(Error::make(ErrorCode::invalid_argument,
                                         "the solution for subtask " + std::to_string(partial.position) + ": " +
                                             owned.error().message));
    }
    auto proof = PartialProof{.position = partial.position, .inside_cases = 0, .failed_outside = false, .sentence = {}};
    for (const auto& sample : owned->inside) {
      const auto oracle = run_program(oracle_source, sample.input, time_limit_ms);
      if (!oracle) {
        return std::unexpected(Error::make(ErrorCode::invalid_argument,
                                           "the oracle " + oracle.error().message + " (n = " +
                                               std::to_string(sample.n) + ")"));
      }
      const auto ran = run_program(partial.source, sample.input, time_limit_ms);
      if (!ran || !cms::tokens_equal(*oracle, *ran)) {
        return std::unexpected(Error::make(
            ErrorCode::invalid_argument,
            "the solution for subtask " + std::to_string(partial.position) +
                (ran ? " does not pass its own group (n = " + std::to_string(sample.n) + ")"
                     : " " + ran.error().message + " (n = " + std::to_string(sample.n) + ")")));
      }
      ++proof.inside_cases;
    }
    for (const auto& sample : owned->outside) {
      const auto oracle = run_program(oracle_source, sample.input, time_limit_ms);
      if (!oracle) {
        return std::unexpected(Error::make(ErrorCode::invalid_argument,
                                           "the oracle " + oracle.error().message + " (n = " +
                                               std::to_string(sample.n) + ")"));
      }
      const auto ran = run_program(partial.source, sample.input, time_limit_ms);
      if (!ran || !cms::tokens_equal(*oracle, *ran)) {
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
                     std::to_string(proof.inside_cases) + " case(s) inside its group and failed a case outside it";
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
