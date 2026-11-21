#include <cms/generate/certify.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <cms/generate/solution.hpp>
#include <cms/generate/test_plan.hpp>

namespace cms::generate {
namespace {

/// ADR 0048: the reference's measured time and memory must stay at or below half the declared limit.


constexpr std::string_view CASES = "the test cases the generator prints";
constexpr std::string_view REFERENCE_PASS = "the reference and the subtask solutions through the checker";
constexpr std::string_view DIFFERENTIAL = "the brute-force differential";
constexpr std::string_view LIMITS = "the reference inside half the declared limits";
constexpr std::string_view NOT_REBUILT = "the test cases could not be re-built";

auto failed(std::string_view subject, std::string_view reason) -> ReRunResult {
  return ReRunResult{.is_passed = false,
                     .sentence = std::string{subject} + ": failed (" + std::string{reason} + ")"};
}

auto held(std::string_view subject, std::string detail) -> ReRunResult {
  return ReRunResult{.is_passed = true, .sentence = std::string{subject} + ": " + std::move(detail)};
}

auto not_run(std::string_view subject) -> ReRunResult {
  return ReRunResult{.is_passed = false,
                     .sentence = std::string{subject} + ": not run (" + std::string{NOT_REBUILT} + ")"};
}

auto sizes_of(const std::vector<GeneratedCase>& cases) -> std::string {
  auto text = std::string{"n = "};
  for (std::size_t index = 0; index < cases.size(); ++index) {
    text += index == 0 ? "" : ", ";
    text += std::to_string(cases[index].n);
  }
  return text;
}

auto count_mismatch(const std::vector<GeneratedCase>& cases,
                    const std::vector<std::pair<std::int32_t, std::int64_t>>& recorded)
    -> std::optional<std::string> {
  for (const auto& [position, count] : recorded) {
    const auto rebuilt = static_cast<std::int64_t>(
        std::count_if(cases.begin(), cases.end(), [position](const GeneratedCase& sample) {
          return sample.subtask_position == position;
        }));
    if (rebuilt != count) {
      return "step 6 recorded " + std::to_string(count) + " case(s) for subtask " + std::to_string(position) +
             ", and the generator's seeds produce " + std::to_string(rebuilt);
    }
  }
  return std::nullopt;
}

auto cases_rebuilt(const std::vector<GeneratedCase>& cases, const ReRunEvidence& evidence) -> ReRunResult {
  if (const auto mismatch = count_mismatch(cases, evidence.recorded_case_counts); mismatch) {
    return failed(CASES, *mismatch);
  }
  return held(CASES, std::to_string(cases.size()) + " case(s) (" + sizes_of(cases) +
                         "), the count step 6 recorded");
}

auto reference_pass(const std::vector<GeneratedCase>& cases, const ReRunEvidence& evidence) -> ReRunResult {
  const auto answer =
      Step6Answer{.generator = TestProgram{.role = "generator", .source = evidence.generator_source, .call = {}},
                  .checker = TestProgram{.role = "checker", .source = evidence.checker_source, .call = {}},
                  .cases = cases};
  const auto scores =
      score_solutions(answer, evidence.reference_source, evidence.partial_sources, evidence.time_limit_ms);
  if (!scores) {
    return failed(REFERENCE_PASS, scores.error().message);
  }
  auto detail = std::string{};
  for (std::size_t index = 0; index < scores->size(); ++index) {
    detail += index == 0 ? "" : "; ";
    detail += (*scores)[index].sentence;
  }
  return held(REFERENCE_PASS, detail);
}

auto differential(const std::vector<GeneratedCase>& cases, const ReRunEvidence& evidence) -> ReRunResult {
  auto answer = Step5Answer{.reference = WrittenSolution{.position = 0,
                                                         .source = evidence.reference_source,
                                                         .call = {}},
                            .partials = {},
                            .reference_answer = {}};
  for (const auto& [position, source] : evidence.partial_sources) {
    answer.partials.push_back(WrittenSolution{.position = position, .source = source, .call = {}});
  }
  auto small_cases = std::vector<SmallCase>{};
  small_cases.reserve(cases.size());
  for (const auto& sample : cases) {
    small_cases.push_back(SmallCase{.input = sample.input, .n = sample.n});
  }
  const auto proofs = check_solutions(answer, evidence.specification, evidence.oracle_source, small_cases,
                                      evidence.time_limit_ms);
  if (!proofs) {
    return failed(DIFFERENTIAL, proofs.error().message);
  }
  auto detail = "the reference agrees with the oracle on " + std::to_string(small_cases.size()) + " case(s) (" +
                sizes_of(cases) + ")";
  for (const auto& proof : *proofs) {
    detail += "; " + proof.sentence;
  }
  return held(DIFFERENTIAL, detail);
}

struct Slowest {
  std::int64_t n{0};
  std::int64_t measured{0};
};

auto limits(const std::vector<GeneratedCase>& cases, const ReRunEvidence& evidence) -> ReRunResult {
  auto slowest_time = Slowest{};
  auto slowest_memory = Slowest{};
  auto slowest_reserved = Slowest{};
  for (const auto& sample : cases) {
    const auto measured = measure_program(evidence.reference_source, sample.input, evidence.time_limit_ms);
    if (!measured) {
      return failed(LIMITS, "the reference " + measured.error().message + " (n = " + std::to_string(sample.n) + ")");
    }
    if (measured->time_ms > slowest_time.measured) {
      slowest_time = Slowest{.n = sample.n, .measured = measured->time_ms};
    }
    if (measured->peak_memory_kb > slowest_memory.measured) {
      slowest_memory = Slowest{.n = sample.n, .measured = measured->peak_memory_kb};
    }
    if (measured->peak_reserved_kb > slowest_reserved.measured) {
      slowest_reserved = Slowest{.n = sample.n, .measured = measured->peak_reserved_kb};
    }
  }
  const auto half_time_ms = evidence.time_limit_ms / REFERENCE_MARGIN_DIVISOR;
  const auto half_used_kb = evidence.memory_used_limit_mb * KB_PER_MB / REFERENCE_MARGIN_DIVISOR;
  const auto half_reserved_kb = evidence.memory_reserved_limit_mb * KB_PER_MB / REFERENCE_MARGIN_DIVISOR;
  auto broken = std::string{};
  const auto add = [&broken](std::string sentence) {
    broken += broken.empty() ? "" : "; ";
    broken += std::move(sentence);
  };
  if (slowest_time.measured > half_time_ms) {
    add("the reference measured " + std::to_string(slowest_time.measured) + " ms at n = " +
        std::to_string(slowest_time.n) + ", above the " + std::to_string(half_time_ms) +
        " ms half of the declared " + std::to_string(evidence.time_limit_ms) + " ms time limit");
  }
  if (slowest_memory.measured > half_used_kb) {
    add("the reference touched " + std::to_string(megabytes(slowest_memory.measured)) + " MB at n = " +
        std::to_string(slowest_memory.n) + ", above the " + std::to_string(half_used_kb / KB_PER_MB) +
        " MB half of the declared " + std::to_string(evidence.memory_used_limit_mb) + " MB used limit");
  }
  if (slowest_reserved.measured > half_reserved_kb) {
    add("the reference mapped " + std::to_string(megabytes(slowest_reserved.measured)) + " MB at n = " +
        std::to_string(slowest_reserved.n) + ", above the " + std::to_string(half_reserved_kb / KB_PER_MB) +
        " MB half of the declared " + std::to_string(evidence.memory_reserved_limit_mb) + " MB reserved limit");
  }
  if (!broken.empty()) {
    return failed(LIMITS, broken);
  }
  return held(LIMITS, std::to_string(slowest_time.measured) + " ms at n = " + std::to_string(slowest_time.n) +
                         ", " + std::to_string(megabytes(slowest_memory.measured)) + " MB used at n = " +
                         std::to_string(slowest_memory.n) + " and " +
                         std::to_string(megabytes(slowest_reserved.measured)) + " MB reserved at n = " +
                         std::to_string(slowest_reserved.n) + ", inside the " + std::to_string(half_time_ms) +
                         " ms, " + std::to_string(half_used_kb / KB_PER_MB) + " MB used and " +
                         std::to_string(half_reserved_kb / KB_PER_MB) + " MB reserved halves of the declared " +
                         std::to_string(evidence.time_limit_ms) + " ms, " +
                         std::to_string(evidence.memory_used_limit_mb) + " MB used and " +
                         std::to_string(evidence.memory_reserved_limit_mb) + " MB reserved limits");
}

}  // namespace

auto re_run_methods(const ReRunEvidence& evidence) -> std::vector<ReRunResult> {
  auto results = std::vector<ReRunResult>{};
  const auto cases = build_cases(evidence.specification, evidence.generator_source, evidence.oracle_source,
                                 evidence.time_limit_ms);
  if (!cases) {
    results.push_back(failed(CASES, cases.error().message));
    results.push_back(not_run(REFERENCE_PASS));
    results.push_back(not_run(DIFFERENTIAL));
    results.push_back(not_run(LIMITS));
    return results;
  }
  results.push_back(cases_rebuilt(*cases, evidence));
  results.push_back(reference_pass(*cases, evidence));
  results.push_back(differential(*cases, evidence));
  results.push_back(limits(*cases, evidence));
  return results;
}

}  // namespace cms::generate
