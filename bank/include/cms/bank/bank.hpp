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

/// @brief One scoring group of a task: points, and the constraint that makes the simpler idea sufficient.
///
/// A subtask carries no limits (ADR 0033 §2): time and memory are the task's, stated once.
struct Subtask {
  std::int32_t position{0};
  std::int32_t points{0};
  std::string constraint_text;
};

/// @brief One task, as the files under `content/tasks/<id>/` describe it.
struct Task {
  std::string id;
  std::int32_t version{0};
  std::string status;
  /// The task type whose fields the authoring form renders; empty when the content names none.
  std::string task_type;
  /// The score mode the content declares, written as the task type's `score_mode` field value; empty when it declares
  /// none. It is what decides whether the task may carry subtasks (ADR 0033 §4).
  std::string score_mode;
  std::string title_en;
  std::string title_th;
  std::int32_t difficulty{0};
  std::int32_t time_limit_ms{0};
  std::int32_t memory_limit_mb{0};
  /// What the task is scored out of, stated once on the task (ADR 0033 §2).
  std::int32_t points{100};
  /// The scoring groups, in position order; empty when the task is not scored by subtask.
  std::vector<Subtask> subtasks;
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

/// @brief One rung of the level list: a named band over the one difficulty scale.
///
/// A rung is a function of the difficulty (ADR 0013 §3), so it is never stored on a task: this is the
/// vocabulary the meter and the page filter both read, and the account stores which rung its learner chose.
struct Level {
  std::string id;
  std::string title_en;
  /// Empty until the content carries a Thai title; the model falls back to English, as every page does.
  std::string title_th;
  std::int32_t difficulty_min{0};
  std::int32_t difficulty_max{0};
};

/// @brief One stage of a route: a difficulty band and the tags a task must carry to belong to it.
struct Stage {
  std::string id;
  std::int32_t position{0};
  std::string title_en;
  std::string title_th;
  std::int32_t difficulty_min{0};
  std::int32_t difficulty_max{0};
  std::vector<std::string> tags;
};

/// @brief One route through the shared module set.
///
/// A track decides order and band, never content: the same module can appear in two routes at different
/// points, and a task never belongs to a track.
struct Track {
  std::string id;
  std::int32_t position{0};
  std::string title_en;
  std::string title_th;
  std::string description_en;
  std::string description_th;
  std::vector<Stage> stages;
  /// The digest of the file entry this track was built from.
  std::string content_hash;
};

/// @brief The whole on-disk bank.
/// One field a task type declares (ADR 0024): the row the form renders and the validator checks.
struct FieldDefinition {
  std::string key;
  std::string kind;
  std::string label_en;
  std::string label_th;
  bool is_required{false};
  std::vector<std::string> choices;
  std::vector<std::string> members;
  std::optional<std::int32_t> minimum;
  std::optional<std::int32_t> maximum;
  std::int32_t position{0};
};

/// One task type and the fields it declares. A new type or field is a row here, not a class.
struct TaskTypeDefinition {
  std::string id;
  std::string title_en;
  std::string title_th;
  std::int32_t position{0};
  std::vector<FieldDefinition> fields;
};

/// @brief Reads one task directory: `task.json`, its statements, its examples and its solution.
///
/// The authoring path and the tests both need one task, not a whole bank; parsing a directory is the seam they share.
///
/// @param directory The `content/tasks/<id>` directory.
/// @return The task with its content hash filled in, or the first thing wrong with it.
[[nodiscard]] auto load_task(const std::filesystem::path& directory) -> Result<Task>;

struct Bank {
  std::vector<Topic> topics;
  std::vector<Task> tasks;
  std::vector<Track> tracks;
  /// The rung list, ordered by its lowest difficulty. The four rungs partition the scale, which the
  /// content gate asserts on the file and the database asserts again on the rows.
  std::vector<Level> levels;
  /// The task types and their fields, imported like the rest of the bank: adding one is a row, not a
  /// rebuild, and the same import runs on every seed.
  std::vector<TaskTypeDefinition> task_types;
  /// One digest over the vocabulary, every task and every track, so an unchanged bank compares equal.
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
