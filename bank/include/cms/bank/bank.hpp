#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <cms/error.hpp>

namespace cms::bank {

/// @brief One entry of the controlled vocabulary every task topic must resolve through.
struct Topic {
  std::string id;
  std::string label_en;
  std::string label_th;
};

/// @brief One input/output pair a student can run the task against.
struct Example {
  std::int32_t ordinal{0};
  std::string input;
  std::string expected_output;
};

/// @brief One task, as the files under `content/tasks/<id>/` describe it.
struct Task {
  std::string id;
  std::int32_t version{0};
  std::string status;
  std::string title_en;
  std::string title_th;
  std::int32_t difficulty{0};
  std::int32_t time_limit_ms{0};
  std::int32_t memory_limit_mb{0};
  std::string checker_kind;
  std::string checker_path;
  std::string origin_kind;
  std::string verification_status;
  /// The reference solution's text, so the bank is a complete record of what the judge runs.
  std::string solution;
  std::vector<std::string> topics;
  /// Locale to statement body, in the order the files were found.
  std::map<std::string, std::string> statements;
  std::vector<Example> examples;
  /// The digest of every file this task was built from.
  std::string content_hash;
};

/// @brief The whole on-disk bank.
struct Bank {
  std::vector<Topic> topics;
  std::vector<Task> tasks;
  /// One digest over the vocabulary and every task's digest, so an unchanged bank compares equal.
  std::string content_hash;
};

/// @brief Reads the bank under @p content_root.
///
/// @param content_root The directory holding `topics.json` and `tasks/`.
/// @return The bank, or the first thing wrong with it, named by file.
///
/// A file is refused rather than repaired: a task whose topic is not in the vocabulary, whose status is
/// not one the schema defines, or whose checker kind cannot be stored would otherwise reach the database
/// and fail there with a constraint name instead of a sentence.
[[nodiscard]] Result<Bank> load(const std::filesystem::path& content_root);

}  // namespace cms::bank
