#include <cms/generate/test_plan.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <cms/generate/sampler.hpp>
#include <cms/generate/solution.hpp>

namespace cms::generate {
namespace {

auto refuse(std::string_view message) -> Error {
  return Error::make(ErrorCode::invalid_argument, message);
}

/// The planted wrong answer: the expected answer with its last digit moved, so a checker that compares answers has
/// to notice. A checker that accepts this one is refused, because it would accept every wrong answer after it.
auto planted_wrong(std::string_view expected) -> std::string {
  auto wrong = std::string{expected};
  for (auto index = wrong.size(); index > 0; --index) {
    const auto digit = wrong[index - 1];
    if (digit >= '0' && digit <= '9') {
      wrong[index - 1] = digit == '9' ? '0' : static_cast<char>(digit + 1);
      return wrong;
    }
  }
  return wrong + "1";
}

auto answers_agree(std::string_view checker, std::string_view expected, std::string_view answer,
                   std::int64_t time_limit_ms) -> Result<bool> {
  const auto verdict = check_answer(checker, expected, answer, time_limit_ms);
  if (!verdict) {
    return std::unexpected(verdict.error());
  }
  return *verdict == "ok";
}

}  // namespace

auto subtask_seeds(const Specification& specification, const SubtaskConstraint& subtask)
    -> Result<std::vector<std::pair<std::int64_t, std::int64_t>>> {
  const auto samples = sample_cases(SampleRequest{.shape = N_THEN_VALUES,
                                                  .bounds = specification.bounds,
                                                  .narrowing = subtask.bounds,
                                                  .seed = CASE_SEED,
                                                  .count = GROUP_CASES,
                                                  .placement = Placement::spread});
  if (!samples) {
    return std::unexpected(samples.error());
  }
  auto seeds = std::vector<std::pair<std::int64_t, std::int64_t>>{};
  auto next = std::int64_t{1};
  for (const auto& sample : *samples) {
    seeds.emplace_back(next++, sample.n);
  }
  return seeds;
}

auto build_cases(const Specification& specification, std::string_view generator, std::string_view oracle,
                 std::int64_t time_limit_ms) -> Result<std::vector<GeneratedCase>> {
  if (specification.subtasks.empty()) {
    return std::unexpected(refuse("the specification names no subtask, so no case is claimed by a group"));
  }
  auto cases = std::vector<GeneratedCase>{};
  for (const auto& subtask : specification.subtasks) {
    const auto seeds = subtask_seeds(specification, subtask);
    if (!seeds) {
      return std::unexpected(Error::make(ErrorCode::invalid_argument,
                                         "subtask " + std::to_string(subtask.position) + ": " +
                                             seeds.error().message));
    }
    for (const auto& [seed, n] : *seeds) {
      const auto request = std::to_string(seed) + " " + std::to_string(n) + "\n";
      const auto input = check_deterministic(generator, request, time_limit_ms);
      if (!input) {
        return std::unexpected(input.error());
      }
      const auto valid = validate_generated(specification, seed, n, *input);
      if (!valid) {
        return std::unexpected(valid.error());
      }
      const auto expected = run_program(oracle, *input, time_limit_ms);
      if (!expected) {
        return std::unexpected(Error::make(ErrorCode::invalid_argument,
                                           "the oracle " + expected.error().message));
      }
      cases.push_back(GeneratedCase{.subtask_position = subtask.position,
                                    .seed = seed,
                                    .n = n,
                                    .input = *input,
                                    .expected = *expected});
    }
  }
  return cases;
}

auto score_solutions(const Step6Answer& answer, std::string_view reference,
                     const std::vector<std::pair<std::int32_t, std::string>>& partials,
                     std::int64_t time_limit_ms) -> Result<std::vector<Score>> {
  if (answer.cases.empty()) {
    return std::unexpected(refuse("step 6 recorded no case to score against"));
  }
  const auto& first = answer.cases.front();
  const auto accepts_right = answers_agree(answer.checker.source, first.expected, first.expected, time_limit_ms);
  if (!accepts_right) {
    return std::unexpected(accepts_right.error());
  }
  if (!*accepts_right) {
    return std::unexpected(refuse("the checker rejects the expected answer itself, so it is not a checker"));
  }
  const auto accepts_wrong =
      answers_agree(answer.checker.source, first.expected, planted_wrong(first.expected), time_limit_ms);
  if (!accepts_wrong) {
    return std::unexpected(accepts_wrong.error());
  }
  if (*accepts_wrong) {
    return std::unexpected(refuse("the checker accepts a planted wrong answer, so it is not a checker: every "
                                  "later proof would be meaningless"));
  }

  auto scores = std::vector<Score>{};
  const auto score_one = [&](std::int32_t position, std::string_view source,
                             bool is_reference) -> Result<void> {
    auto accepted = std::int64_t{0};
    auto rejected_outside = false;
    for (const auto& sample : answer.cases) {
      const auto ran = run_program(source, sample.input, time_limit_ms);
      if (!ran) {
        return std::unexpected(Error::make(
            ErrorCode::invalid_argument,
            "the solution " + std::to_string(position) + " " + ran.error().message + " (subtask " +
                std::to_string(sample.subtask_position) + ")"));
      }
      const auto agrees = answers_agree(answer.checker.source, sample.expected, *ran, time_limit_ms);
      if (!agrees) {
        return std::unexpected(agrees.error());
      }
      const auto own_group = is_reference || sample.subtask_position == position;
      if (*agrees && own_group) {
        ++accepted;
      }
      if (!*agrees && !is_reference) {
        rejected_outside = true;
      }
    }
    if (is_reference) {
      if (accepted != static_cast<std::int64_t>(answer.cases.size())) {
        return std::unexpected(refuse("the reference is accepted on " + std::to_string(accepted) + " of " +
                                      std::to_string(answer.cases.size()) + " cases, so it does not score full marks"));
      }
      scores.push_back(Score{.position = 0,
                             .accepted = accepted,
                             .total = static_cast<std::int64_t>(answer.cases.size()),
                             .sentence = "the reference is accepted on every case (" + std::to_string(accepted) +
                                         " of " + std::to_string(answer.cases.size()) + ")"});
      return {};
    }
    if (accepted == 0) {
      return std::unexpected(refuse("the solution for subtask " + std::to_string(position) +
                                    " is accepted on none of its own cases, so it earns nothing"));
    }
    if (!rejected_outside) {
      return std::unexpected(refuse("the solution for subtask " + std::to_string(position) +
                                    " is accepted on every case, so it is not a partial: its points are not earned "
                                    "by a weaker solution"));
    }
    scores.push_back(Score{.position = position,
                           .accepted = accepted,
                           .total = static_cast<std::int64_t>(answer.cases.size()),
                           .sentence = "subtask " + std::to_string(position) + ": accepted on " +
                                       std::to_string(accepted) + " of its own cases and rejected on a case outside"});
    return {};
  };

  const auto reference_score = score_one(0, reference, true);
  if (!reference_score) {
    return std::unexpected(reference_score.error());
  }
  for (const auto& [position, source] : partials) {
    const auto scored = score_one(position, source, false);
    if (!scored) {
      return std::unexpected(scored.error());
    }
  }
  for (const auto& subtask : answer.cases) {
    const auto found = std::find_if(scores.begin(), scores.end(), [&](const Score& score) {
      return score.position == subtask.subtask_position;
    });
    if (found == scores.end() && subtask.subtask_position != 0) {
      return std::unexpected(refuse("subtask " + std::to_string(subtask.subtask_position) +
                                    " has cases but no solution that earns it"));
    }
  }
  return scores;
}

auto write_test_plan(ModelClient& client, const Specification& specification, std::string_view oracle,
                     std::string_view reference,
                     const std::vector<std::pair<std::int32_t, std::string>>& partials,
                     std::int64_t time_limit_ms) -> Result<TestPlanResult> {
  const auto generator_request = generator_prompt(specification);
  if (!generator_request) {
    return std::unexpected(generator_request.error());
  }
  const auto generator = client.complete(*generator_request);
  if (!generator) {
    return std::unexpected(generator.error());
  }
  auto answer = Step6Answer{};
  const auto generator_program = parse_test_program(generator->text, "generator", generator->call);
  if (!generator_program) {
    return std::unexpected(generator_program.error());
  }
  answer.generator = *generator_program;

  const auto checker_request = checker_prompt(specification);
  if (!checker_request) {
    return std::unexpected(checker_request.error());
  }
  const auto checker = client.complete(*checker_request);
  if (!checker) {
    return std::unexpected(checker.error());
  }
  const auto checker_program = parse_test_program(checker->text, "checker", checker->call);
  if (!checker_program) {
    return std::unexpected(checker_program.error());
  }
  answer.checker = *checker_program;

  const auto validator_request = validator_prompt(specification);
  if (!validator_request) {
    return std::unexpected(validator_request.error());
  }
  const auto validator = client.complete(*validator_request);
  if (!validator) {
    return std::unexpected(validator.error());
  }
  const auto validator_program = parse_test_program(validator->text, "validator", validator->call);
  if (!validator_program) {
    return std::unexpected(validator_program.error());
  }
  answer.validator = *validator_program;

  const auto cases = build_cases(specification, answer.generator.source, oracle, time_limit_ms);
  if (!cases) {
    return std::unexpected(cases.error());
  }
  answer.cases = *cases;
  const auto scores = score_solutions(answer, reference, partials, time_limit_ms);
  if (!scores) {
    return std::unexpected(scores.error());
  }
  return TestPlanResult{.answer = std::move(answer), .scores = *scores};
}

auto MemoryTestPlanSink::record(std::int64_t job_id, const Step6Answer& answer, const std::vector<Score>& scores)
    -> Result<std::size_t> {
  (void)scores;
  job_id_ = job_id;
  answer_ = answer;
  return answer_.cases.size() + 3;
}

}  // namespace cms::generate
