#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <cms/error.hpp>

namespace cms::judge {

/// @brief Everything a draft task is written from.
///
/// Every field is required and named: a specification with a hole is refused rather than written, because a
/// task directory that the loader cannot read is worse than no task at all.
struct TaskSpec {
  std::string id;
  std::string title_en;
  std::string title_th;
  std::vector<std::string> topics;
  std::int32_t difficulty{0};
  std::int32_t time_ms{0};
  std::int32_t memory_mb{0};
  std::string checker;
  std::string statement_en;
  std::string statement_th;
  std::string example_input;
  std::string example_output;
  std::string solution;
};

/// @brief Writes one draft task directory in the shape the content loader reads.
///
/// @param spec The task. Every field must be present and the id must be a URL-safe lower-case name.
/// @param directory The directory to create; it must not already exist.
/// @return Success, or the first field that is missing or the reason the directory cannot be written.
///
/// The output is a pure function of the specification: no clock, no counter, no random device. Two runs with
/// the same specification produce byte-identical files, which is what lets a draft be reviewed as a diff.
[[nodiscard]] Result<void> generate_task(const TaskSpec& spec, const std::filesystem::path& directory);

}  // namespace cms::judge
