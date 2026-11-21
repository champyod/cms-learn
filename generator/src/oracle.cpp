#include <cms/generate/specification.hpp>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <fcntl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <charconv>
#include <unistd.h>

namespace cms::generate {
namespace {

constexpr std::string_view COMPILER = "c++";
constexpr std::string_view SOURCE = "main.cpp";
constexpr std::string_view PROGRAM = "oracle";
constexpr std::string_view CASE_IN = "case.in";
constexpr std::string_view CASE_OUT = "case.out";
constexpr std::string_view COMPILE_ERROR = "compile.err";
constexpr int COMPILE_WALL_SECONDS = 120;
constexpr std::int64_t FLOOR_WALL_SECONDS = 10;
constexpr std::int64_t GRACE_SECONDS = 2;
constexpr std::int64_t MILLISECONDS_PER_SECOND = 1000;
/// How often the parent samples the child's own high-water mark while it runs. Small enough that a program of a few
/// milliseconds is sampled many times, large enough that a ten-second one does not spend the parent's time on it.
constexpr long POLL_NANOSECONDS = 100'000;

auto read_text(const std::filesystem::path& path) -> std::string {
  auto stream = std::ifstream{path, std::ios::binary};
  auto buffer = std::ostringstream{};
  buffer << stream.rdbuf();
  return buffer.str();
}

auto write_text(const std::filesystem::path& path, std::string_view text) -> bool {
  auto stream = std::ofstream{path, std::ios::trunc | std::ios::binary};
  stream << text;
  return static_cast<bool>(stream);
}

auto first_line(std::string_view text) -> std::string {
  const auto end = text.find('\n');
  const auto line = text.substr(0, end == std::string_view::npos ? text.size() : end);
  return std::string{line};
}

/// @return @p key's value in `/proc/<pid>/status` in kB, or 0 when it cannot be read.
///
/// The two marks this reads are `VmHWM` (peak resident, what the program touched) and `VmPeak` (peak virtual, the
/// address space it mapped). Both belong to the address space that exists now, and `execve` replaces that space —
/// so a mark read after the program replaced the fork's copy is the program's own, never the pages it inherited.
/// Both only rise, so the largest sample taken while the program runs is its peak.
auto status_kb(pid_t pid, std::string_view key) -> std::int64_t {
  auto stream = std::ifstream{"/proc/" + std::to_string(pid) + "/status"};
  auto line = std::string{};
  while (std::getline(stream, line)) {
    if (line.rfind(key, 0) != 0) {
      continue;
    }
    auto value = std::int64_t{0};
    const auto digits = std::string_view{line}.substr(key.size());
    const auto first = digits.find_first_of("0123456789");
    if (first == std::string_view::npos) {
      return 0;
    }
    const auto end = digits.find_first_not_of("0123456789", first);
    const auto parsed = std::from_chars(digits.data() + first, digits.data() + (end == std::string_view::npos ? digits.size() : end), value);
    return parsed.ec == std::errc{} ? value : 0;
  }
  return 0;
}

auto peak_resident_kb(pid_t pid) -> std::int64_t { return status_kb(pid, "VmHWM:"); }

auto peak_reserved_kb(pid_t pid) -> std::int64_t { return status_kb(pid, "VmPeak:"); }

/// @return Whether @p pid already runs @p program, so a sample taken before `execve` — when the child still holds the
/// parent's pages — is never mistaken for the program's own footprint.
auto is_running(pid_t pid, std::string_view program) -> bool {
  const auto link = "/proc/" + std::to_string(pid) + "/exe";
  auto target = std::array<char, 4096>{};
  const auto length = readlink(link.c_str(), target.data(), target.size() - 1);
  if (length <= 0) {
    return false;
  }
  const auto path = std::string_view{target.data(), static_cast<std::size_t>(length)};
  return path.size() > program.size() &&
         path.compare(path.size() - program.size(), program.size(), program) == 0 &&
         path[path.size() - program.size() - 1] == '/';
}

/// @return The exit status, or -1 when the child could not be started or did not return in time.
///
/// When @p measurement is named the parent samples the child's own high-water mark while it runs. `wait4`'s
/// `ru_maxrss` cannot answer: it is the peak of the whole process, including the pages the fork inherits from its
/// parent — a sanitizer-built test binary is ~140 MB resident — so a three-megabyte program measured that way reports
/// its parent's size, and ADR 0048's half-limit margin contradicts itself for any small task.
auto run_child(const std::vector<std::string>& arguments, const std::filesystem::path& directory,
               const std::filesystem::path& input, const std::filesystem::path& output,
               int wall_seconds, ProgramMeasurement* measurement, std::string_view program = {}) -> int {
  const auto child = fork();
  if (child < 0) {
    return -1;
  }
  if (child == 0) {
    if (chdir(directory.c_str()) != 0) {
      _exit(127);
    }
    const auto input_fd = open(input.c_str(), O_RDONLY);
    const auto output_fd = open(output.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (input_fd < 0 || output_fd < 0) {
      _exit(127);
    }
    dup2(input_fd, STDIN_FILENO);
    dup2(output_fd, STDOUT_FILENO);
    alarm(static_cast<unsigned int>(wall_seconds));
    auto argv = std::vector<char*>{};
    argv.reserve(arguments.size() + 1);
    for (const auto& argument : arguments) {
      argv.push_back(const_cast<char*>(argument.c_str()));
    }
    argv.push_back(nullptr);
    execvp(argv.front(), argv.data());
    _exit(127);
  }
  auto status = 0;
  if (measurement == nullptr) {
    while (waitpid(child, &status, 0) < 0) {
      if (errno != EINTR) {
        return -1;
      }
    }
  } else {
    const auto interval = timespec{.tv_sec = 0, .tv_nsec = POLL_NANOSECONDS};
    while (true) {
      const auto waited = waitpid(child, &status, WNOHANG);
      if (waited == child) {
        break;
      }
      if (waited < 0) {
        if (errno == EINTR) {
          continue;
        }
        return -1;
      }
      if (is_running(child, program)) {
        const auto peak = peak_resident_kb(child);
        if (peak > measurement->peak_memory_kb) {
          measurement->peak_memory_kb = peak;
        }
        const auto reserved = peak_reserved_kb(child);
        if (reserved > measurement->peak_reserved_kb) {
          measurement->peak_reserved_kb = reserved;
        }
      }
      nanosleep(&interval, nullptr);
    }
  }
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  return -1;
}

auto scratch_directory() -> std::filesystem::path {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("cms-oracle-" + std::to_string(static_cast<long>(getpid())) + "-" + std::to_string(now));
}

/// @return What @p source answered for @p input, and in @p measurement what the run itself cost when it is named.
auto run_program_measured(std::string_view source, std::string_view input, std::int64_t time_limit_ms,
                          ProgramMeasurement* measurement) -> Result<std::string> {
  if (source.find("int main") == std::string_view::npos) {
    return std::unexpected(Error::make(ErrorCode::invalid_argument,
                                       "the program is not a complete program: it has no int main"));
  }
  const auto directory = scratch_directory();
  struct Cleanup {
    const std::filesystem::path& path;
    ~Cleanup() {
      auto ignored = std::error_code{};
      std::filesystem::remove_all(path, ignored);
    }
  } cleanup_guard{directory};
  auto cleanup = std::error_code{};
  std::filesystem::create_directories(directory, cleanup);
  if (cleanup) {
    return std::unexpected(
        Error::make(ErrorCode::unavailable, "no scratch directory for the program: " + cleanup.message()));
  }
  if (!write_text(directory / SOURCE, source) || !write_text(directory / CASE_IN, input)) {
    return std::unexpected(Error::make(ErrorCode::unavailable, "the program's scratch files could not be written"));
  }
  const auto compile = run_child({std::string{COMPILER}, "-std=c++17", "-O2", "-pipe", "-o",
                                  std::string{PROGRAM}, std::string{SOURCE}},
                                 directory, "/dev/null", directory / COMPILE_ERROR, COMPILE_WALL_SECONDS, nullptr);
  if (compile != 0) {
    return std::unexpected(Error::make(
        ErrorCode::invalid_argument,
        "does not compile: " + first_line(read_text(directory / COMPILE_ERROR))));
  }
  const auto wall = std::max<std::int64_t>(FLOOR_WALL_SECONDS,
                                           time_limit_ms / MILLISECONDS_PER_SECOND + GRACE_SECONDS);
  auto measurement_value = ProgramMeasurement{};
  const auto started = std::chrono::steady_clock::now();
  const auto ran = run_child({"./" + std::string{PROGRAM}}, directory, directory / CASE_IN,
                             directory / CASE_OUT, static_cast<int>(wall),
                             measurement == nullptr ? nullptr : &measurement_value, PROGRAM);
  const auto finished = std::chrono::steady_clock::now();
  if (ran != 0) {
    return std::unexpected(Error::make(ErrorCode::invalid_argument,
                                       "did not finish the case within " + std::to_string(wall) + " s (exit " +
                                           std::to_string(ran) + ")"));
  }
  if (measurement != nullptr) {
    measurement->time_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(finished - started).count();
    if (measurement_value.peak_memory_kb == 0 || measurement_value.peak_reserved_kb == 0) {
      return std::unexpected(Error::make(
          ErrorCode::unavailable,
          "answered the case but its peak used and reserved memory could not both be read from /proc/<pid>/status, "
          "so the limits it has to fit cannot be measured"));
    }
    measurement->peak_memory_kb = measurement_value.peak_memory_kb;
    measurement->peak_reserved_kb = measurement_value.peak_reserved_kb;
  }
  auto answer = read_text(directory / CASE_OUT);
  if (answer.find_first_not_of(" \t\r\n") == std::string::npos) {
    return std::unexpected(Error::make(ErrorCode::invalid_argument, "answered the case with nothing"));
  }
  return answer;
}

}  // namespace

auto run_program(std::string_view source, std::string_view input, std::int64_t time_limit_ms)
    -> Result<std::string> {
  return run_program_measured(source, input, time_limit_ms, nullptr);
}

/// @brief What a run cost: the wall time, and the peak used and reserved memory of the program itself.
///
/// The two memory numbers are the program's own, read from `/proc/<pid>/status:VmHWM` (touched pages) and `:VmPeak`
/// (mapped address space) while it runs. Both are high-water marks of the address space that exists at that moment,
/// and `execve` replaces that space, so the samples taken after the program has exec'd measure it alone. They are read
/// this way and not from `wait4`'s `ru_maxrss` because that mark covers the whole life of the process: the pages the
/// fork inherits from its parent count as its own until `execve` runs, and for a sanitizer-built test binary that is
/// ~140 MB, so a three-megabyte program measured that way reported its parent's size and ADR 0048's half-limit margin
/// could never pass for a small task.
auto measure_program(std::string_view source, std::string_view input, std::int64_t time_limit_ms)
    -> Result<ProgramMeasurement> {
  auto measurement = ProgramMeasurement{};
  const auto answer = run_program_measured(source, input, time_limit_ms, &measurement);
  if (!answer) {
    return std::unexpected(answer.error());
  }
  return measurement;
}

auto check_oracle(std::string_view source, std::string_view smallest_input, std::int64_t time_limit_ms)
    -> Result<OracleCheck> {
  auto answer = run_program(source, smallest_input, time_limit_ms);
  if (!answer) {
    return std::unexpected(answer.error());
  }
  return OracleCheck{.smallest_input = std::string{smallest_input}, .answer = std::move(*answer)};
}

}  // namespace cms::generate
