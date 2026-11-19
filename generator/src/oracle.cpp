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

/// @return The exit status, or -1 when the child could not be started or did not return in time.
///
/// Whenever @p usage is named the child is reaped with `wait4`, so its peak memory is read from the kernel rather
/// than sampled from `/proc`, which a fast program has already left by the time a reader looks.
auto run_child(const std::vector<std::string>& arguments, const std::filesystem::path& directory,
               const std::filesystem::path& input, const std::filesystem::path& output,
               int wall_seconds, rusage* usage) -> int {
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
  while ((usage == nullptr ? waitpid(child, &status, 0) : wait4(child, &status, 0, usage)) < 0) {
    if (errno != EINTR) {
      return -1;
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
  auto usage = rusage{};
  const auto started = std::chrono::steady_clock::now();
  const auto ran = run_child({"./" + std::string{PROGRAM}}, directory, directory / CASE_IN,
                             directory / CASE_OUT, static_cast<int>(wall), measurement == nullptr ? nullptr : &usage);
  const auto finished = std::chrono::steady_clock::now();
  if (ran != 0) {
    return std::unexpected(Error::make(ErrorCode::invalid_argument,
                                       "did not finish the case within " + std::to_string(wall) + " s (exit " +
                                           std::to_string(ran) + ")"));
  }
  if (measurement != nullptr) {
    measurement->time_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(finished - started).count();
    measurement->peak_memory_kb = usage.ru_maxrss;
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
