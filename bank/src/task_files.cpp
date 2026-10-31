#include "task.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "detail.hpp"
#include "task_manifest.hpp"

namespace cms::bank {
namespace {

using detail::Hasher;
using detail::known;
using detail::read_text;

constexpr std::string_view TASK_FILE = "task.json";
constexpr std::string_view SOLUTIONS_DIR = "solutions";
constexpr std::string_view EXAMPLES_DIR = "examples";
constexpr std::string_view SOLUTION_NAME = "main.cpp";
constexpr std::string_view STATEMENT_PREFIX = "statement.";
constexpr std::string_view STATEMENT_SUFFIX = ".md";
constexpr std::string_view EXAMPLE_INPUT_SUFFIX = ".in";
constexpr std::string_view EXAMPLE_OUTPUT_SUFFIX = ".out";

constexpr std::array<std::string_view, 2> LOCALES{"en", "th"};

auto starts_with(std::string_view text, std::string_view prefix) -> bool {
  return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

auto ends_with(std::string_view text, std::string_view suffix) -> bool {
  return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

auto load_statements(const std::filesystem::path& directory, std::string_view where, Task& task)
    -> Result<void> {
  for (const auto& entry : std::filesystem::directory_iterator{directory}) {
    const auto name = entry.path().filename().string();
    if (!starts_with(name, STATEMENT_PREFIX) || !ends_with(name, STATEMENT_SUFFIX)) {
      continue;
    }
    const auto locale = name.substr(STATEMENT_PREFIX.size(),
                                    name.size() - STATEMENT_PREFIX.size() - STATEMENT_SUFFIX.size());
    const auto allowed = known(locale, LOCALES, "statement locale", where);
    if (!allowed) {
      return allowed;
    }
    const auto body = read_text(entry.path());
    if (!body) {
      return std::unexpected(body.error());
    }
    task.statements[locale] = *body;
  }
  if (task.statements.empty()) {
    return std::unexpected(
        Error::make(ErrorCode::invalid_argument, std::string{where} + ": no statement file"));
  }
  return {};
}


/// Collects the numbered inputs an example directory holds, so the pair below is a lookup rather than a
/// second directory scan.
auto example_inputs(const std::filesystem::path& examples_dir, std::string_view where)
    -> Result<std::map<std::int32_t, std::filesystem::path>> {
  auto inputs = std::map<std::int32_t, std::filesystem::path>{};
  for (const auto& entry : std::filesystem::directory_iterator{examples_dir}) {
    const auto name = entry.path().filename().string();
    if (!ends_with(name, EXAMPLE_INPUT_SUFFIX)) {
      continue;
    }
    const auto stem = name.substr(0, name.size() - EXAMPLE_INPUT_SUFFIX.size());
    try {
      inputs[std::stoi(stem)] = entry.path();
    } catch (const std::exception&) {
      return std::unexpected(Error::make(
          ErrorCode::invalid_argument,
          std::string{where} + ": example \"" + name + "\" is not numbered"));
    }
  }
  if (inputs.empty()) {
    return std::unexpected(
        Error::make(ErrorCode::invalid_argument, std::string{where} + ": no examples"));
  }
  return inputs;
}

auto load_examples(const std::filesystem::path& directory, std::string_view where, Task& task)
    -> Result<void> {
  const auto examples_dir = directory / EXAMPLES_DIR;
  if (!std::filesystem::is_directory(examples_dir)) {
    return std::unexpected(
        Error::make(ErrorCode::invalid_argument, std::string{where} + ": no examples directory"));
  }

  const auto inputs = example_inputs(examples_dir, where);
  if (!inputs) {
    return std::unexpected(inputs.error());
  }
  for (const auto& [ordinal, input_path] : *inputs) {
    const auto expected_path =
        examples_dir / (std::to_string(ordinal) + std::string{EXAMPLE_OUTPUT_SUFFIX});
    const auto input = read_text(input_path);
    if (!input) {
      return std::unexpected(input.error());
    }
    const auto expected = read_text(expected_path);
    if (!expected) {
      return std::unexpected(expected.error());
    }
    task.examples.push_back(
        Example{.ordinal = ordinal, .input = *input, .expected_output = *expected});
  }
  return {};
}


auto load_solution(const std::filesystem::path& directory, std::string_view where, Task& task)
    -> Result<void> {
  const auto solution = read_text(directory / SOLUTIONS_DIR / SOLUTION_NAME);
  if (!solution) {
    return std::unexpected(Error::make(
        ErrorCode::invalid_argument, std::string{where} + ": no reference solution at " +
                                         std::string{SOLUTIONS_DIR} + "/" + std::string{SOLUTION_NAME}));
  }
  task.solution = *solution;
  return {};
}


auto digest_task(const std::filesystem::path& directory, Task& task) -> Result<void> {
  auto hasher = Hasher{};
  const auto manifest = read_text(directory / TASK_FILE);
  if (!manifest) {
    return std::unexpected(manifest.error());
  }
  hasher.add_field(*manifest);
  for (const auto& [locale, body] : task.statements) {
    hasher.add_field(locale);
    hasher.add_field(body);
  }
  for (const auto& example : task.examples) {
    hasher.add_field(std::to_string(example.ordinal));
    hasher.add_field(example.input);
    hasher.add_field(example.expected_output);
  }
  hasher.add_field(task.solution);
  task.content_hash = hasher.digest();
  return {};
}


/// The directory name is the id a URL will carry, so it is checked here rather than by a route later.
auto check_id_shape(const Task& task) -> Result<void> {
  const auto where = task.id;
  if (task.id.empty() || std::isalnum(static_cast<unsigned char>(task.id.front())) == 0) {
    return std::unexpected(Error::make(
        ErrorCode::invalid_argument, where + ": the id must start with a letter or digit"));
  }
  for (const auto character : task.id) {
    const auto allowed =
        std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '-';
    if (!allowed || std::isupper(static_cast<unsigned char>(character)) != 0) {
      return std::unexpected(Error::make(
          ErrorCode::invalid_argument,
          where + ": the id may hold only lower-case letters, digits and '-'"));
    }
  }
  return {};
}

}  // namespace

auto load_task(const std::filesystem::path& directory) -> Result<Task> {
  auto task = Task{};
  const auto where = directory.filename().string();

  if (const auto step = parse_manifest(directory, task); !step) {
    return std::unexpected(step.error());
  }
  if (const auto step = check_id_shape(task); !step) {
    return std::unexpected(step.error());
  }
  if (const auto step = load_statements(directory, where, task); !step) {
    return std::unexpected(step.error());
  }
  if (const auto step = load_examples(directory, where, task); !step) {
    return std::unexpected(step.error());
  }
  if (const auto step = load_solution(directory, where, task); !step) {
    return std::unexpected(step.error());
  }
  if (const auto step = digest_task(directory, task); !step) {
    return std::unexpected(step.error());
  }
  return task;
}

}  // namespace cms::bank
