#include <cms/judge/supervisor.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include <poll.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

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

auto now() -> std::chrono::steady_clock::time_point { return std::chrono::steady_clock::now(); }

/// Opens a pidfd for @p child, or returns a negative value when the kernel does not provide one.
///
/// Called through `syscall` rather than `pidfd_open`: glibc declares that function with C++ linkage instead
/// of `extern "C"`, so the declared call links against a mangled name no C library exports.
auto open_process_descriptor(pid_t child) -> int {
  return static_cast<int>(::syscall(SYS_pidfd_open, child, 0));
}

/// Signals the process @p descriptor refers to. The kernel resolves the pid once, so a recycled pid can
/// never receive this signal.
auto signal_process_descriptor(int descriptor, int signal_number) -> bool {
  return ::syscall(SYS_pidfd_send_signal, descriptor, signal_number, nullptr, 0) == 0;
}

/// The child's pidfd: a descriptor bound to that one process for its whole life, so a signal sent through it
/// cannot reach a pid the kernel has already recycled. That is the race the pid-based kill leaves open.
class process_descriptor {
 public:
  explicit process_descriptor(pid_t child) : child_(child), descriptor_(open_process_descriptor(child)) {}
  ~process_descriptor() {
    if (descriptor_ >= 0) {
      ::close(descriptor_);
    }
  }

  process_descriptor(const process_descriptor&) = delete;
  process_descriptor& operator=(const process_descriptor&) = delete;
  process_descriptor(process_descriptor&&) = delete;
  process_descriptor& operator=(process_descriptor&&) = delete;

  [[nodiscard]] auto is_open() const -> bool { return descriptor_ >= 0; }
  [[nodiscard]] auto child() const -> pid_t { return child_; }
  [[nodiscard]] auto descriptor() const -> int { return descriptor_; }

  /// Sends @p signal_number to this process alone, never to a process that inherited its pid.
  auto send(int signal_number) const -> bool {
    return signal_process_descriptor(descriptor_, signal_number);
  }

 private:
  pid_t child_;
  int descriptor_;
};

/// Waits up to @p milliseconds for @p child. Returns false when the deadline passed first.
///
/// The wait is a poll over the child's pidfd, which becomes readable when the child exits: no tick, and no
/// wake-up at all while the program is still running.
auto wait_within(const process_descriptor& child, std::int64_t milliseconds) -> bool {
  const auto deadline = now() + std::chrono::milliseconds{milliseconds};
  auto status = 0;
  while (true) {
    const auto seen = ::waitpid(child.child(), &status, WNOHANG);
    if (seen == child.child() || (seen < 0 && errno != EINTR)) {
      return true;
    }
    const auto remaining = deadline - now();
    if (remaining <= std::chrono::steady_clock::duration::zero()) {
      return false;
    }
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count();
    const auto timeout = static_cast<int>(
        std::min(left, static_cast<std::int64_t>(std::numeric_limits<int>::max())));
    auto watch = ::pollfd{.fd = child.descriptor(), .events = POLLIN, .revents = 0};
    if (::poll(&watch, 1, timeout) < 0 && errno != EINTR) {
      return false;
    }
  }
}

/// Sends SIGTERM to the group, waits the grace period, then SIGKILL. This is the escalation the
/// fork-bomb test exists for: a program that ignores the polite request still ends.
auto terminate(const process_descriptor& child) -> void {
  // The group kill reaches the sandbox's descendants; the pidfd signal reaches the child itself.
  ::kill(-child.child(), SIGTERM);
  (void)child.send(SIGTERM);
  if (wait_within(child, TERMINATION_GRACE_MILLISECONDS)) {
    return;
  }
  ::kill(-child.child(), SIGKILL);
  (void)child.send(SIGKILL);
  auto status = 0;
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

  const auto descriptor = process_descriptor{child};
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
