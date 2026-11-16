#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <cms/error.hpp>

namespace cms::judge {

/// @brief One test case a task owns, with the digests the judge can check later.
struct CaseEntry {
  /// Paths relative to the task directory, so a manifest stays valid when the tree moves.
  std::string input;
  std::string output;
  std::string input_digest;
  std::string output_digest;
};

/// @brief What a task's test data amounts to, as the judge reads it.
struct Manifest {
  /// The task directory's own name, so a manifest says which task it describes.
  std::string task;
  /// The digest of the reference executable's bytes. Where that file sits is not part of a task's identity: two
  /// checkouts of the same tree must describe the same task (ADR 0023 §2 says the same for a task's bytes).
  std::string reference_digest;
  std::vector<CaseEntry> cases;
};

/// @brief Renders @p manifest as the JSON the judge and CI both read.
[[nodiscard]] std::string render_manifest(const Manifest& manifest);

/// @brief Writes @p manifest to @p path, creating the directory it names.
[[nodiscard]] Result<void> write_manifest(const std::filesystem::path& path,
                                          const Manifest& manifest);

}  // namespace cms::judge
