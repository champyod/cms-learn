#pragma once

#include <atomic>
#include <filesystem>
#include <random>
#include <string>

namespace cms::testing {

/// @brief A scratch directory no other running case can share.
///
/// ctest registers every case as its own test and runs several at once, and every one of them invokes
/// the same binary. A fixed name under the temporary directory is therefore shared state between two
/// cases that never asked to share anything: one removes the tree while the other is reading it.
[[nodiscard]] inline auto scratch_directory(std::string_view name) -> std::filesystem::path {
  static auto sequence = std::atomic<unsigned>{0};
  static auto device = std::random_device{};
  const auto suffix = std::to_string(device()) + "-" + std::to_string(sequence++);
  return std::filesystem::temp_directory_path() / (std::string{name} + "-" + suffix);
}

}  // namespace cms::testing
