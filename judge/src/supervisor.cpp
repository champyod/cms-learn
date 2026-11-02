#include <cms/judge/supervisor.hpp>

#include <array>
#include <csignal>
#include <cstddef>
#include <fstream>
#include <string>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

#include <cms/judge/child_watch.hpp>

namespace cms::judge {
namespace {

constexpr int TERMINATION_GRACE_MILLISECONDS = 200;
constexpr std::size_t META_BYTES = 64 * 1024;
constexpr std::size_t CAPTURE_BYTES = 8 * 1024;

auto to_arguments(const std::vector<std::string>& values) -> std::vector<char*> {
  auto pointers = std::vector<char*>{};
  pointers.reserve(values.size() + 1);
  for (const auto& value : values) {
    pointers.push_back(const_cast<char*>(value.c_str()));
  }
  pointers.push_back(nullptr);
  return pointers;
}

/// Forks and execs @p argv in its own process group, so the whole tree can be signalled at once.
auto spawn(const std::vector<std::string>& values, int stdout_fd) -> pid_t {
  const auto child = ::fork();
  if (child != 0) {
    return child;
  }
  ::setpgid(0, 0);
  if (stdout_fd >= 0) {
    ::dup2(stdout_fd, STDOUT_FILENO);
  }
  // The sandbox gets the three standard streams and nothing else the supervisor holds open.
  ::closefrom(3);
  auto pointers = to_arguments(values);
  ::execvp(pointers[0], pointers.data());
  ::_exit(127);
}

/// Reads at most @p limit bytes from @p fd.
auto capture(int fd, std::size_t limit) -> std::string {
  auto collected = std::string{};
  auto buffer = std::array<char, 4096>{};
  while (collected.size() < limit) {
    const auto count = ::read(fd, buffer.data(), buffer.size());
    if (count <= 0) {
      break;
    }
    collected.append(buffer.data(), static_cast<std::size_t>(count));
  }
  return collected;
}

/// Sends SIGTERM to the group, waits the grace period, then SIGKILL. This is the escalation the
/// fork-bomb test exists for: a program that ignores the polite request still ends.
auto terminate(const child_process& child) -> void {
  // The group kill reaches the sandbox's descendants; the pidfd signal reaches the child itself.
  ::kill(-child.child(), SIGTERM);
  (void)child.send(SIGTERM);
  auto status = 0;
  if (wait_within(child, TERMINATION_GRACE_MILLISECONDS, &status)) {
    return;
  }
  ::kill(-child.child(), SIGKILL);
  (void)child.send(SIGKILL);
  ::waitpid(child.child(), &status, 0);
}

/// Runs a short isolate command (`--init`, `--cleanup`) and returns its stdout and exit status.
auto run_isolate_capture(const std::vector<std::string>& arguments, std::string& captured) -> int {
  auto ends = std::array<int, 2>{-1, -1};
  if (::pipe(ends.data()) != 0) {
    return -1;
  }
  const auto child = spawn(arguments, ends[1]);
  ::close(ends[1]);
  if (child < 0) {
    ::close(ends[0]);
    return -1;
  }
  captured = capture(ends[0], CAPTURE_BYTES);
  ::close(ends[0]);

  auto status = 0;
  if (::waitpid(child, &status, 0) != child) {
    return -1;
  }
  if (!WIFEXITED(status)) {
    return -1;
  }
  return WEXITSTATUS(status);
}

auto read_meta(const std::filesystem::path& path) -> std::map<std::string, std::string> {
  auto entries = std::map<std::string, std::string>{};
  auto stream = std::ifstream{path};
  auto line = std::string{};
  while (std::getline(stream, line)) {
    const auto separator = line.find(':');
    if (separator == std::string::npos) {
      continue;
    }
    entries.emplace(line.substr(0, separator), line.substr(separator + 1));
  }
  return entries;
}

auto trimmed(std::string_view text) -> std::string_view {
  const auto first = text.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) {
    return {};
  }
  const auto last = text.find_last_not_of(" \t\r\n");
  return text.substr(first, last - first + 1);
}

}  // namespace

/// Seconds as isolate spells them, built from integers: `1.500`, never `1.4999999`.
auto seconds_text(std::int64_t milliseconds) -> std::string {
  const auto seconds = std::to_string(milliseconds / 1000);
  const auto fraction = std::to_string(milliseconds % 1000);
  return seconds + "." + std::string(3 - fraction.size(), '0') + fraction;
}

auto isolate_arguments(const RunSpec& spec) -> std::vector<std::string> {
  const auto box = spec.box_dir.string();
  return {
      "--box-id",    spec.box_id,
      "--meta",      (spec.box_dir / spec.meta).string(),
      "--stdin",     (spec.box_dir / spec.input).string(),
      "--stdout",    (spec.box_dir / spec.output).string(),
      "--stderr",    (spec.box_dir / spec.error).string(),
      "--time",      seconds_text(spec.time_limit_ms),
      "--wall-time", seconds_text(spec.wall_limit_ms),
      "--mem",       std::to_string(spec.memory_limit_kb),
      "--processes", std::to_string(spec.process_limit),
      "--dir",       "box=" + box,
  };
}

auto prepare_box(const std::string& box_id, const std::string& isolate_path)
    -> Result<std::filesystem::path> {
  auto captured = std::string{};
  const auto status = run_isolate_capture({isolate_path, "--init", "--box-id", box_id}, captured);
  if (status != 0) {
    return std::unexpected(Error::make(ErrorCode::unavailable,
                                       "isolate could not initialise box " + box_id));
  }
  const auto directory = std::string{trimmed(captured)};
  if (directory.empty()) {
    return std::unexpected(
        Error::make(ErrorCode::unavailable, "isolate initialised box " + box_id + " without a path"));
  }
  return std::filesystem::path{directory};
}

auto discard_box(const std::string& box_id, const std::string& isolate_path) -> Result<void> {
  auto captured = std::string{};
  if (run_isolate_capture({isolate_path, "--cleanup", "--box-id", box_id}, captured) != 0) {
    return std::unexpected(Error::make(ErrorCode::unavailable,
                                       "isolate could not clean up box " + box_id));
  }
  return {};
}

auto run_sandboxed(const RunSpec& spec, const std::string& isolate_path) -> Result<RunOutcome> {
  // The executable has to be at the head: exec takes argv[0] as the program to run, not as a flag.
  auto arguments = std::vector<std::string>{isolate_path};
  const auto flags = isolate_arguments(spec);
  arguments.insert(arguments.end(), flags.begin(), flags.end());
  arguments.push_back("--run");
  arguments.push_back("--");
  arguments.push_back(spec.program);

  // A report from an earlier run in the same box must not be read as this run's. Removing it first makes
  // "no status" mean what it says.
  const auto meta_path = spec.box_dir / spec.meta;
  std::error_code discarded;
  std::filesystem::remove(meta_path, discarded);

  const auto child = spawn(arguments, -1);
  if (child < 0) {
    return std::unexpected(
        Error::make(ErrorCode::unavailable, "the sandbox could not be started"));
  }

  const auto descriptor = child_process{child};
  if (!descriptor.is_open()) {
    // Without a pidfd the wall limit could only be enforced by waking up to poll, and a supervisor that
    // cannot enforce it must say so rather than let the program run unsupervised.
    ::kill(-child, SIGKILL);
    auto status = 0;
    ::waitpid(child, &status, 0);
    return std::unexpected(
        Error::make(ErrorCode::unavailable, "this kernel cannot supervise the sandbox"));
  }

  auto outcome = RunOutcome{};
  if (!wait_within(descriptor, spec.wall_limit_ms)) {
    outcome.is_killed = true;
    terminate(descriptor);
  }

  const auto meta = read_meta(meta_path);
  outcome.meta = meta;
  const auto status = meta.find("status");
  outcome.status = status == meta.end() ? std::string{} : std::string{trimmed(status->second)};
  if (outcome.status.empty() && !outcome.is_killed) {
    return std::unexpected(
        Error::make(ErrorCode::unavailable, "the sandbox wrote no status; the run did not happen"));
  }
  return outcome;
}

}  // namespace cms::judge
