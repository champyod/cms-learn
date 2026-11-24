#include <cms/generate/test_plan.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cms/generate/data_frame.hpp>

namespace cms::generate {
namespace {

constexpr std::string_view GENERATOR_ROLE = "generator";
constexpr std::string_view CHECKER_ROLE = "checker";
constexpr std::string_view VALIDATOR_ROLE = "validator";
constexpr std::int64_t CHECKER_WALL_SECONDS = 10;

auto refuse(std::string_view message) -> Error {
  return Error::make(ErrorCode::invalid_argument, message);
}

auto write_text(const std::filesystem::path& path, std::string_view text) -> bool {
  auto stream = std::ofstream{path, std::ios::trunc | std::ios::binary};
  stream << text;
  return static_cast<bool>(stream);
}

auto scratch_directory() -> std::filesystem::path {
  static auto counter = std::int64_t{0};
  ++counter;
  return std::filesystem::temp_directory_path() /
         ("cms-checker-" + std::to_string(static_cast<long>(getpid())) + "-" + std::to_string(counter));
}

/// @return The exit status, or -1 when the child could not be started or timed out.
auto run_child(const std::vector<std::string>& arguments, const std::filesystem::path& directory,
               const std::vector<std::string>& files, int wall_seconds) -> int {
  const auto child = fork();
  if (child < 0) {
    return -1;
  }
  if (child == 0) {
    if (chdir(directory.c_str()) != 0) {
      _exit(127);
    }
    alarm(static_cast<unsigned int>(wall_seconds));
    auto argv = std::vector<char*>{};
    argv.reserve(arguments.size() + files.size() + 1);
    for (const auto& argument : arguments) {
      argv.push_back(const_cast<char*>(argument.c_str()));
    }
    for (const auto& file : files) {
      argv.push_back(const_cast<char*>(file.c_str()));
    }
    argv.push_back(nullptr);
    execvp(argv.front(), argv.data());
    _exit(127);
  }
  auto status = 0;
  while (waitpid(child, &status, 0) < 0) {
    if (errno != EINTR) {
      return -1;
    }
  }
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

}  // namespace

auto parse_test_program(std::string_view answer, std::string_view role, const ModelCall& call)
    -> Result<TestProgram> {
  if (answer.find("int main") == std::string_view::npos) {
    return std::unexpected(refuse("the " + std::string{role} + " is not a complete program: it has no int main"));
  }
  return TestProgram{.role = std::string{role}, .source = std::string{answer}, .call = call};
}

auto generator_prompt(const Specification& specification) -> Result<std::string> {
  const auto bounds = frame_data(ModelData{.label = "the bounds",
                                           .text = "n from " + std::to_string(specification.bounds.min_n) + " to " +
                                                   std::to_string(specification.bounds.max_n) + ", values " +
                                                   std::to_string(specification.bounds.value_min) + ".." +
                                                   std::to_string(specification.bounds.value_max)});
  if (!bounds) {
    return std::unexpected(bounds.error());
  }
  return "You are step 6 of a task-generation pipeline. Write the test-case generator: a complete C++17 program\n"
         "that reads two integers from standard input, a seed and n, and prints one case of exactly n integers in\n"
         "the declared bounds. The same seed and n must always print the same bytes.\n\n" +
         *bounds + "\n\nAnswer with the program and nothing else.\n";
}

auto validator_prompt(const Specification& specification) -> Result<std::string> {
  const auto input = frame_data(ModelData{.label = "the input shape", .text = specification.input});
  const auto bounds = frame_data(ModelData{.label = "the bounds",
                                           .text = "n from " + std::to_string(specification.bounds.min_n) + " to " +
                                                   std::to_string(specification.bounds.max_n) + ", values " +
                                                   std::to_string(specification.bounds.value_min) + ".." +
                                                   std::to_string(specification.bounds.value_max)});
  if (!input) {
    return std::unexpected(input.error());
  }
  if (!bounds) {
    return std::unexpected(bounds.error());
  }
  return "You are step 6 of a task-generation pipeline. Write the validator: a complete C++17 program built\n"
         "against testlib that reads one case from standard input and refuses it when it does not have the\n"
         "declared shape or leaves the declared bounds. Call registerValidation, read the case from inf, and on\n"
         "every violation quitf(_fail, ...) with a message naming the value and the rule it broke. It must accept\n"
         "every case the generator prints, and it must refuse a case that is one value short, one value over, or\n"
         "empty: a validator that accepts everything proves nothing about the inputs stored later.\n\n" +
         *input + "\n\n" + *bounds + "\n\nAnswer with the program and nothing else.\n";
}

auto checker_prompt(const Specification& specification) -> Result<std::string> {
  const auto output = frame_data(ModelData{.label = "the expected answer", .text = specification.output});
  if (!output) {
    return std::unexpected(output.error());
  }
  return "You are step 6 of a task-generation pipeline. Write the checker: a complete C++17 program that reads\n"
         "two file paths from its arguments, the expected answer and the contestant's answer, and exits 0 when\n"
         "they agree as answers and 1 when they do not. It must compare the answers the way the output says, not\n"
         "compare whole files: a checker that accepts everything makes every later proof meaningless.\n\n" +
         *output + "\n\nAnswer with the program and nothing else.\n";
}

auto validate_generated(const Specification& specification, std::int64_t seed, std::int64_t n,
                        std::string_view input) -> Result<void> {
  auto stream = std::istringstream{std::string{input}};
  auto declared = std::int64_t{0};
  if (!(stream >> declared)) {
    return std::unexpected(refuse("the generator printed nothing for seed " + std::to_string(seed) + ", n = " +
                                  std::to_string(n)));
  }
  if (declared != n) {
    return std::unexpected(refuse("the generated input for seed " + std::to_string(seed) + " declares n = " +
                                  std::to_string(declared) + ", and the case was asked for n = " + std::to_string(n)));
  }
  for (std::int64_t index = 0; index < n; ++index) {
    auto value = std::int64_t{0};
    if (!(stream >> value)) {
      return std::unexpected(refuse("the generated input for seed " + std::to_string(seed) + " has " +
                                    std::to_string(index) + " of " + std::to_string(n) + " values"));
    }
    if (value < specification.bounds.value_min || value > specification.bounds.value_max) {
      return std::unexpected(refuse("the generated input for seed " + std::to_string(seed) + " holds " +
                                    std::to_string(value) + ", outside the declared values " +
                                    std::to_string(specification.bounds.value_min) + ".." +
                                    std::to_string(specification.bounds.value_max)));
    }
  }
  auto extra = std::string{};
  if (stream >> extra) {
    return std::unexpected(refuse("the generated input for seed " + std::to_string(seed) + " holds more than " +
                                  std::to_string(n) + " values"));
  }
  return {};
}

auto check_deterministic(std::string_view generator, std::string_view input, std::int64_t time_limit_ms)
    -> Result<std::string> {
  const auto first = run_program(generator, input, time_limit_ms);
  if (!first) {
    return std::unexpected(Error::make(ErrorCode::invalid_argument,
                                       "the generator " + first.error().message));
  }
  const auto second = run_program(generator, input, time_limit_ms);
  if (!second) {
    return std::unexpected(Error::make(ErrorCode::invalid_argument,
                                       "the generator " + second.error().message));
  }
  if (*first != *second) {
    return std::unexpected(Error::make(
        ErrorCode::invalid_argument,
        "the generator is not deterministic: the same input produced two different outputs, \"" +
            std::string{*first} + "\" and \"" + std::string{*second} + "\""));
  }
  return *first;
}

auto check_answer(std::string_view checker, std::string_view expected, std::string_view answer,
                  std::int64_t time_limit_ms) -> Result<std::string> {
  (void)time_limit_ms;
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
    return std::unexpected(Error::make(ErrorCode::unavailable, "no scratch directory for the checker"));
  }
  if (!write_text(directory / "checker.cpp", checker) || !write_text(directory / "expected.txt", expected) ||
      !write_text(directory / "answer.txt", answer)) {
    return std::unexpected(Error::make(ErrorCode::unavailable, "the checker's scratch files could not be written"));
  }
  const auto compile = run_child({"c++", "-std=c++17", "-O2", "-o", "checker", "checker.cpp"}, directory, {},
                                 120);
  if (compile != 0) {
    return std::unexpected(Error::make(ErrorCode::invalid_argument, "the checker does not compile"));
  }
  const auto ran = run_child({"./checker"}, directory, {"expected.txt", "answer.txt"}, CHECKER_WALL_SECONDS);
  if (ran == 0) {
    return std::string{"ok"};
  }
  if (ran == 1) {
    return std::string{"wrong"};
  }
  return std::unexpected(Error::make(ErrorCode::invalid_argument,
                                     "the checker exited " + std::to_string(ran) + ", and a checker answers 0 or 1"));
}

}  // namespace cms::generate
