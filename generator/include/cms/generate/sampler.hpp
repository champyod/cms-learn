#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <cms/error.hpp>
#include <cms/subtask.hpp>

namespace cms::generate {

/// The one input shape this pipeline builds: the size, then that many values (ADR 0040).
inline constexpr std::string_view N_THEN_VALUES = "n_then_values";

/// How many cases one group carries: its smallest n and its largest, which is what the differential and the test
/// plan both ask for (ADR 0040, 0044).
inline constexpr std::int64_t GROUP_CASES = 2;

/// The seed the pipeline's own cases are drawn with: an argument, never the clock.
inline constexpr std::int64_t CASE_SEED = 0;

/// @brief Where a group's cases sit inside the interval the group admits.
enum class Placement : std::uint8_t {
  /// Every case at the group's smallest n, which is the example and the oracle's check.
  smallest,
  /// Every case at the group's largest n.
  largest,
  /// @p count cases from the smallest n to the largest, oldest first.
  spread,
};

/// @brief One request for a group's cases: the declared shape, the task's bounds, the narrowing the group names, the
/// seed, how many cases, and where their n sits.
struct SampleRequest {
  std::string_view shape{N_THEN_VALUES};
  cms::TaskBounds bounds;
  /// Empty asks for the task's own interval, which is what the example and the oracle's check sample.
  cms::SubtaskBounds narrowing{};
  std::int64_t seed{0};
  std::int64_t count{1};
  Placement placement{Placement::smallest};
};

/// @brief One drawn case: the size it was drawn at, and the input this pipeline builds.
struct Sample {
  std::int64_t n{0};
  std::string input;
};

/// @brief Draws @p request.count cases from the interval the request names (ADR 0040).
///
/// The same request draws the same bytes every time: the seed is an argument, never the clock, and the values are
/// drawn inside the narrowing the request names. A shape this sampler does not build, a narrowing the task does not
/// admit, or a count below one is refused by name rather than guessed.
[[nodiscard]] auto sample_cases(const SampleRequest& request) -> Result<std::vector<Sample>>;

/// @brief The one case the example and the oracle's check are drawn at: the smallest the task's bounds admit.
///
/// It is one function so the step that runs the oracle and the step that stores the example cannot ask for two
/// different "smallest" cases.
[[nodiscard]] auto smallest_case(const cms::TaskBounds& bounds) -> Result<Sample>;

}  // namespace cms::generate
