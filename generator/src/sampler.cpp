#include <cms/generate/sampler.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cms::generate {
namespace {

/// A fixed odd multiplier: the draw is reproducible across platforms and needs no library generator, which is what
/// "the same seed produces the same bytes" means here.
constexpr std::uint64_t GOLDEN = 0x9e3779b97f4a7c15ULL;
constexpr std::uint64_t SECOND_MULTIPLIER = 0x2545f4914f6cdd1dULL;
constexpr unsigned SHIFT_A = 12;
constexpr unsigned SHIFT_B = 25;
constexpr unsigned SHIFT_C = 27;

auto refuse(std::string_view message) -> Error {
  return Error::make(ErrorCode::invalid_argument, message);
}

auto next_random(std::uint64_t& state) -> std::uint64_t {
  state ^= state >> SHIFT_A;
  state ^= state << SHIFT_B;
  state ^= state >> SHIFT_C;
  return state * SECOND_MULTIPLIER;
}

auto state_for(std::int64_t seed, std::int64_t case_index) -> std::uint64_t {
  auto state = static_cast<std::uint64_t>(seed) * GOLDEN +
               static_cast<std::uint64_t>(case_index) * SECOND_MULTIPLIER + GOLDEN;
  return state == 0 ? GOLDEN : state;
}

/// @return The size one case is drawn at, from the interval and the placement.
auto size_at(const cms::TaskBounds& interval, std::int64_t index, const SampleRequest& request)
    -> std::int64_t {
  switch (request.placement) {
    case Placement::smallest:
      return interval.min_n;
    case Placement::largest:
      return interval.max_n;
    case Placement::spread:
      break;
  }
  if (request.count <= 1 || interval.max_n == interval.min_n) {
    return index + 1 == request.count ? interval.max_n : interval.min_n;
  }
  const auto step = (interval.max_n - interval.min_n) / (request.count - 1);
  return index + 1 == request.count ? interval.max_n : interval.min_n + index * step;
}

/// @return One input of @p size values, each drawn inside @p interval from @p state.
auto input_at(std::int64_t size, const cms::TaskBounds& interval, std::uint64_t& state) -> std::string {
  const auto span = static_cast<std::uint64_t>(interval.value_max - interval.value_min) + 1;
  auto input = std::to_string(size) + "\n";
  for (std::int64_t index = 0; index < size; ++index) {
    const auto value = interval.value_min + static_cast<std::int64_t>(next_random(state) % span);
    input += std::to_string(value);
    input += index + 1 == size ? '\n' : ' ';
  }
  return input;
}

}  // namespace

auto sample_cases(const SampleRequest& request) -> Result<std::vector<Sample>> {
  if (request.shape != N_THEN_VALUES) {
    return std::unexpected(refuse("the sampler builds " + std::string{N_THEN_VALUES} + " inputs, and the " +
                                  "specification declares \"" + std::string{request.shape} + "\""));
  }
  if (request.count < 1) {
    return std::unexpected(refuse("a sampler is asked for at least one case, and this request asks for " +
                                  std::to_string(request.count)));
  }
  if (request.bounds.min_n < 1) {
    return std::unexpected(refuse("the task's bounds admit n from " + std::to_string(request.bounds.min_n) +
                                  ", and a case needs at least one value"));
  }
  if (!cms::is_empty(request.narrowing)) {
    if (const auto refused = cms::narrowing_refusal(request.narrowing, request.bounds); refused.has_value()) {
      return std::unexpected(refuse("the subtask's narrowing is refused: " + *refused));
    }
  }
  const auto interval = cms::is_empty(request.narrowing) ? request.bounds : cms::narrowed(request.narrowing, request.bounds);
  auto samples = std::vector<Sample>{};
  samples.reserve(static_cast<std::size_t>(request.count));
  for (std::int64_t index = 0; index < request.count; ++index) {
    const auto size = size_at(interval, index, request);
    if (size < 1 || size > request.bounds.max_n) {
      return std::unexpected(refuse("the case at index " + std::to_string(index) + " would hold " +
                                    std::to_string(size) + " values, outside the task's bound " +
                                    std::to_string(request.bounds.max_n)));
    }
    auto state = state_for(request.seed, index);
    samples.push_back(Sample{.n = size, .input = input_at(size, interval, state)});
  }
  return samples;
}

auto smallest_case(const cms::TaskBounds& bounds) -> Result<Sample> {
  const auto samples = sample_cases(SampleRequest{.shape = N_THEN_VALUES,
                                                  .bounds = bounds,
                                                  .seed = CASE_SEED,
                                                  .count = 1,
                                                  .placement = Placement::smallest});
  if (!samples || samples->empty()) {
    return std::unexpected(samples ? refuse("the sampler drew no case") : samples.error());
  }
  return samples->front();
}

}  // namespace cms::generate
