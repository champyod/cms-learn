#include <cms/judge/verify.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <fcntl.h>
#include <sodium.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cms/judge/child_watch.hpp>

namespace cms::judge {
namespace {

/// A reference solution is content, and a looping one must fail its case rather than freeze the verifier.
constexpr std::int64_t REFERENCE_TIMEOUT_MILLISECONDS = 60'000;

constexpr std::size_t HEX_DIGITS_PER_BYTE = 2;
constexpr unsigned NIBBLE_SHIFT = 4;
constexpr unsigned NIBBLE_MASK = 0x0fu;

auto digest(std::string_view text) -> std::string {
  auto bytes = std::array<unsigned char, crypto_hash_sha256_BYTES>{};
  crypto_hash_sha256(bytes.data(), reinterpret_cast<const unsigned char*>(text.data()), text.size());
  constexpr std::string_view DIGITS = "0123456789abcdef";
  auto hex = std::string{};
  hex.reserve(bytes.size() * HEX_DIGITS_PER_BYTE);
  for (const auto byte : bytes) {
    hex.push_back(DIGITS[byte >> NIBBLE_SHIFT]);
    hex.push_back(DIGITS[byte & NIBBLE_MASK]);
  }
  return hex;
}

auto read_text(const std::filesystem::path& path) -> Result<std::string> {
  auto stream = std::ifstream{path, std::ios::binary};
  if (!stream) {
    return std::unexpected(Error::make(ErrorCode::not_found, "cannot read " + path.string()));
  }
  auto buffer = std::ostringstream{};
  buffer << stream.rdbuf();
  return buffer.str();
}

/// Runs @p program with @p input on its standard input and @p output as its standard output.
/// Returns the exit status, or -1 when the program could not be started.
auto run_program(const std::string& program, const std::filesystem::path& input,
                 const std::filesystem::path& output) -> int {
  const auto child = ::fork();
  if (child != 0) {
    if (child < 0) {
      return -1;
    }
    const auto descriptor = child_process{child};
    auto status = 0;
    if (!descriptor.is_open() ||
        !wait_within(descriptor, REFERENCE_TIMEOUT_MILLISECONDS, &status)) {
      // A reference that never exits would otherwise hang every caller of verify_task, and promote_task
      // verifies before it publishes.
      ::kill(child, SIGKILL);
      ::waitpid(child, &status, 0);
      return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  }

  const auto input_fd = ::open(input.c_str(), O_RDONLY);
  const auto output_fd = ::open(output.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (input_fd < 0 || output_fd < 0) {
    ::_exit(126);
  }
  ::dup2(input_fd, STDIN_FILENO);
  ::dup2(output_fd, STDOUT_FILENO);
  // Only the three standard streams belong to the reference; everything else the verifier holds stays out.
  ::closefrom(3);
  ::execl(program.c_str(), program.c_str(), static_cast<char*>(nullptr));
  ::_exit(127);
}

auto case_paths(const std::filesystem::path& task_dir) -> std::vector<std::filesystem::path> {
  auto inputs = std::vector<std::filesystem::path>{};
  const auto examples = task_dir / "examples";
  for (const auto& entry : std::filesystem::directory_iterator{examples}) {
    if (entry.path().extension() == ".in") {
      inputs.push_back(entry.path());
    }
  }
  std::ranges::sort(inputs);
  return inputs;
}

/// Runs the reference over one case and compares what it produced with what the case expects.
auto check_case(const std::filesystem::path& task_dir, const std::filesystem::path& reference,
                const std::filesystem::path& input, const std::filesystem::path& scratch)
    -> Result<CaseEntry> {
  const auto name = task_dir.filename().string() + "/" + input.filename().string();
  auto expected_path = input;
  expected_path.replace_extension(".out");

  std::error_code discarded;
  if (run_program(reference.string(), input, scratch) != 0) {
    std::filesystem::remove(scratch, discarded);
    return std::unexpected(Error::make(ErrorCode::unavailable,
                                       name + ": the reference did not exit cleanly"));
  }

  const auto produced = read_text(scratch);
  std::filesystem::remove(scratch, discarded);
  const auto expected = read_text(expected_path);
  const auto source = read_text(input);
  if (!produced || !expected || !source) {
    return std::unexpected(
        Error::make(ErrorCode::unavailable, name + ": an unreadable case file"));
  }
  if (*produced != *expected) {
    return std::unexpected(Error::make(
        ErrorCode::invalid_argument, name + ": the reference does not produce the expected output"));
  }

  return CaseEntry{.input = "examples/" + input.filename().string(),
                   .output = "examples/" + expected_path.filename().string(),
                   .input_digest = digest(*source),
                   .output_digest = digest(*expected)};
}

}  // namespace

auto verify_task(const std::filesystem::path& task_dir, const std::filesystem::path& reference)
    -> Result<Manifest> {
  const auto inputs = case_paths(task_dir);
  if (inputs.empty()) {
    return std::unexpected(
        Error::make(ErrorCode::invalid_argument, task_dir.string() + ": the task has no case"));
  }

  auto manifest = Manifest{.task = task_dir.filename().string(),
                           .reference_digest = digest(reference.string()),
                           .cases = {}};
  const auto scratch = std::filesystem::temp_directory_path() /
                       ("cms-verify-" + std::to_string(::getpid()) + ".out");

  for (const auto& input : inputs) {
    const auto entry = check_case(task_dir, reference, input, scratch);
    if (!entry) {
      return std::unexpected(entry.error());
    }
    manifest.cases.push_back(*entry);
  }
  return manifest;
}

}  // namespace cms::judge
