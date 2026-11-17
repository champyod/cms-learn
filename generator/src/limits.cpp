#include <cms/generate/specification.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cms::generate {
namespace {

constexpr std::int64_t BYTES_PER_VALUE = 8;
constexpr std::int64_t BYTES_PER_MB = 1024 * 1024;

auto refuse(std::string_view message) -> Error {
  return Error::make(ErrorCode::invalid_argument, message);
}

/// The canonical complexity forms this step can turn into a number. A form it does not know is refused by name
/// rather than approximated: a derived limit that rests on a guessed exponent is a limit chosen freely.
auto operations_at_canonical(std::string_view form, double n) -> std::optional<double> {
  if (form == "1") {
    return 1.0;
  }
  if (form == "log n") {
    return std::log2(std::max(1.0, n));
  }
  if (form == "n") {
    return n;
  }
  if (form == "n log n") {
    return n * std::log2(std::max(1.0, n));
  }
  if (form == "n log^2 n") {
    return n * std::pow(std::log2(std::max(1.0, n)), 2.0);
  }
  if (form == "n^2") {
    return n * n;
  }
  if (form == "n^2 log n") {
    return n * n * std::log2(std::max(1.0, n));
  }
  if (form == "n^3") {
    return n * n * n;
  }
  if (form == "n sqrt n") {
    return n * std::sqrt(n);
  }
  if (form == "2^n") {
    return std::exp2(std::min(n, 1000.0));
  }
  return std::nullopt;
}

}  // namespace

auto operations_at(std::string_view complexity, std::int64_t n) -> Result<double> {
  auto form = std::string{complexity};
  const auto open = form.find_first_not_of(" \t");
  const auto close = form.find_last_not_of(" \t");
  if (open == std::string::npos) {
    return std::unexpected(refuse("the plan names no complexity to derive a limit from"));
  }
  form = form.substr(open, close - open + 1);
  if (form.size() > 2 && form.starts_with("O(") && form.ends_with(')')) {
    form = form.substr(2, form.size() - 3);
  }
  auto spaced = std::string{};
  for (const auto character : form) {
    if (character == ' ') {
      if (!spaced.empty() && spaced.back() != ' ') {
        spaced.push_back(' ');
      }
      continue;
    }
    spaced.push_back(character);
  }
  const auto known = operations_at_canonical(spaced, static_cast<double>(n));
  if (!known) {
    return std::unexpected(Error::make(
        ErrorCode::invalid_argument,
        "the plan's complexity \"" + std::string{complexity} +
            "\" is not a form this step can derive a limit from; it reads O(1), O(log n), O(n), O(n log n), "
            "O(n log^2 n), O(n^2), O(n^2 log n), O(n^3), O(n sqrt n) and O(2^n)"));
  }
  return *known;
}

auto derive_limit(std::string_view complexity, const Bounds& bounds) -> Result<DerivedLimit> {
  if (bounds.min_n <= 0 || bounds.max_n < bounds.min_n) {
    return std::unexpected(refuse("the plan names no bound to derive a limit from: the bounds are n from " +
                                  std::to_string(bounds.min_n) + " to " + std::to_string(bounds.max_n)));
  }
  const auto operations = operations_at(complexity, bounds.max_n);
  if (!operations) {
    return std::unexpected(operations.error());
  }
  const auto milliseconds = std::ceil(*operations / static_cast<double>(OPERATIONS_PER_MILLISECOND));
  if (milliseconds > static_cast<double>(TIME_LIMIT_CEILING_MS)) {
    return std::unexpected(Error::make(
        ErrorCode::invalid_argument,
        "the derivation lands outside the allowed band: " + std::string{complexity} + " at n = " +
            std::to_string(bounds.max_n) + " needs " + std::to_string(static_cast<std::int64_t>(milliseconds)) +
            " ms, and a task allows " + std::to_string(TIME_LIMIT_FLOOR_MS) + ".." +
            std::to_string(TIME_LIMIT_CEILING_MS) + " ms; the plan and the bound have to be reconciled"));
  }
  const auto values = std::ceil(static_cast<double>(bounds.max_n * BYTES_PER_VALUE * 2) / BYTES_PER_MB);
  if (values > static_cast<double>(MEMORY_LIMIT_CEILING_MB)) {
    return std::unexpected(Error::make(
        ErrorCode::invalid_argument,
        "the derivation lands outside the allowed band: n = " + std::to_string(bounds.max_n) +
            " needs " + std::to_string(static_cast<std::int64_t>(values)) + " MB, and a task allows " +
            std::to_string(MEMORY_LIMIT_FLOOR_MB) + ".." + std::to_string(MEMORY_LIMIT_CEILING_MB) + " MB"));
  }
  auto derived = DerivedLimit{};
  derived.time_limit_ms = std::max(TIME_LIMIT_FLOOR_MS, static_cast<std::int64_t>(milliseconds));
  derived.memory_limit_mb = std::max(MEMORY_LIMIT_FLOOR_MB, static_cast<std::int64_t>(values));
  derived.derivation = std::string{complexity} + " at n = " + std::to_string(bounds.max_n) + " is " +
                       std::to_string(static_cast<std::int64_t>(*operations)) + " operations; at " +
                       std::to_string(OPERATIONS_PER_MILLISECOND) + " operations per millisecond that is " +
                       std::to_string(static_cast<std::int64_t>(milliseconds)) + " ms, inside " +
                       std::to_string(TIME_LIMIT_FLOOR_MS) + ".." + std::to_string(TIME_LIMIT_CEILING_MS) +
                       " ms, so the time limit is " + std::to_string(derived.time_limit_ms) + " ms; n values double "
                       "in memory are " + std::to_string(static_cast<std::int64_t>(values)) + " MB, inside " +
                       std::to_string(MEMORY_LIMIT_FLOOR_MB) + ".." + std::to_string(MEMORY_LIMIT_CEILING_MB) +
                       " MB, so the memory limit is " + std::to_string(derived.memory_limit_mb) + " MB";
  return derived;
}

}  // namespace cms::generate
