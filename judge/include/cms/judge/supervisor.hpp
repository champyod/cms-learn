#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <cms/error.hpp>

namespace cms::judge {

/// @brief One run of one submission, expressed as the facts isolate needs.
struct RunSpec {
  /// The box this run may use. Box-id N maps to a fixed UID range, so an id is never shared by two runs.
  std::string box_id{"0"};
  /// The directory the program and its files live in; isolate reads and writes inside it.
  std::filesystem::path box_dir;
  /// The program to run, relative to the box directory.
  std::string program{"./solution"};
  /// Arguments the program is run with, after @c program and inside the sandbox.
  std::vector<std::string> arguments;
  /// CPU time the program may use, in milliseconds.
  std::int64_t time_limit_ms{1000};
  /// How long the supervisor waits before escalating, in milliseconds. Deliberately larger than the CPU
  /// limit: a program can be asleep, not running, and still hold the box.
  std::int64_t wall_limit_ms{10000};
  /// Address space ceiling in kilobytes.
  std::int64_t memory_limit_kb{262144};
  /// Processes the program may create. One unless the task needs otherwise.
  int process_limit{1};
  /// Files isolate redirects the program's streams to, relative to the box directory.
  std::string input{"input.txt"};
  std::string output{"output.txt"};
  std::string error{"error.txt"};
  /// Where isolate writes its report.
  std::string meta{"meta.txt"};
};

/// @brief What the sandbox reported.
struct RunOutcome {
  /// True when the supervisor had to kill the process tree itself.
  bool is_killed{false};
  /// The keys isolate wrote to the meta file, verbatim.
  std::map<std::string, std::string> meta;
  /// The value of `status`, which is what isolate actually decided: `OK`, `TO`, `SG`, `RE`, `ML`, `XX`.
  std::string status;
};

/// @brief The isolate argument list for @p spec, without the program and its own arguments.
///
/// Every flag here is from the v2.7 manual: `-b/--box-id`, `-M/--meta`, `-i/--stdin`, `-o/--stdout`,
/// `-r/--stderr`, `-d/--dir`, `-m/--mem`, `-t/--time`, `-w/--wall-time`, `-p/--processes`.
[[nodiscard]] std::vector<std::string> isolate_arguments(const RunSpec& spec);

/// @brief Runs one case under isolate and enforces the supervisor's own ceiling.
///
/// @param spec The run.
/// @param isolate_path The isolate executable.
/// @return What the sandbox reported, or an error when it could not be started or cleaned up.
///
/// The supervisor does not trust the sandbox to end: it waits until the wall limit, sends `SIGTERM`, waits
/// a grace period, then sends `SIGKILL` to the process group, and only then reads the report. A program
/// that ignores both signals is the case the fork-bomb test exists for.
[[nodiscard]] Result<RunOutcome> run_sandboxed(const RunSpec& spec,
                                               const std::string& isolate_path);

/// @brief Initialises a box, returning the directory isolate created.
[[nodiscard]] Result<std::filesystem::path> prepare_box(const std::string& box_id,
                                                        const std::string& isolate_path);

/// @brief Removes a box, whether or not the run finished.
[[nodiscard]] Result<void> discard_box(const std::string& box_id, const std::string& isolate_path);

}  // namespace cms::judge
