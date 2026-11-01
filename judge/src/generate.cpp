#include <cms/judge/generate.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace cms::judge {
namespace {

constexpr std::int32_t MINIMUM_DIFFICULTY = 800;
constexpr std::int32_t MAXIMUM_DIFFICULTY = 3500;
constexpr std::int32_t DIFFICULTY_STEP = 100;

auto is_identifier(std::string_view text) -> bool {
  if (text.empty() || std::isalnum(static_cast<unsigned char>(text.front())) == 0) {
    return false;
  }
  return std::ranges::all_of(text, [](char character) {
    return std::islower(static_cast<unsigned char>(character)) != 0 ||
           std::isdigit(static_cast<unsigned char>(character)) != 0 || character == '-';
  });
}

/// The fields a task cannot exist without, checked before anything is written.
auto refuse_incomplete(const TaskSpec& spec) -> Result<void> {
  const auto missing = [](std::string_view name) {
    return std::unexpected(Error::make(ErrorCode::invalid_argument,
                                       "a task needs " + std::string{name}));
  };
  if (!is_identifier(spec.id)) {
    return std::unexpected(Error::make(ErrorCode::invalid_argument,
                                       "the id must be lower-case letters, digits and '-'"));
  }
  if (spec.title_en.empty()) return missing("an English title");
  if (spec.title_th.empty()) return missing("a Thai title");
  if (spec.topics.empty()) return missing("at least one topic");
  if (spec.checker.empty()) return missing("a checker");
  if (spec.statement_en.empty()) return missing("an English statement");
  if (spec.statement_th.empty()) return missing("a Thai statement");
  if (spec.example_input.empty()) return missing("an example input");
  if (spec.example_output.empty()) return missing("an example output");
  if (spec.solution.empty()) return missing("a reference solution");
  if (spec.difficulty < MINIMUM_DIFFICULTY || spec.difficulty > MAXIMUM_DIFFICULTY ||
      spec.difficulty % DIFFICULTY_STEP != 0) {
    return std::unexpected(Error::make(ErrorCode::invalid_argument,
                                       "the difficulty must be a multiple of 100 within 800 to 3500"));
  }
  if (spec.time_ms < 100 || spec.memory_mb < 16) {
    return std::unexpected(Error::make(ErrorCode::invalid_argument, "the limits are out of range"));
  }
  return {};
}

/// The manifest, written field by field in a fixed order so two runs diff to nothing.
auto manifest_text(const TaskSpec& spec) -> std::string {
  auto topics = std::string{};
  for (std::size_t index = 0; index < spec.topics.size(); ++index) {
    topics += (index == 0 ? "" : ", ") + std::string{"\""} + spec.topics[index] + "\"";
  }
  auto text = std::string{};
  text += "{\n";
  text += "  \"id\": \"" + spec.id + "\",\n";
  text += "  \"version\": 1,\n";
  text += "  \"status\": \"draft\",\n";
  text += "  \"title\": { \"en\": \"" + spec.title_en + "\", \"th\": \"" + spec.title_th + "\" },\n";
  text += "  \"topics\": [" + topics + "],\n";
  text += "  \"difficulty\": " + std::to_string(spec.difficulty) + ",\n";
  text += "  \"locales\": [\"en\", \"th\"],\n";
  text += "  \"limits\": { \"time_ms\": " + std::to_string(spec.time_ms) + ", \"memory_mb\": " +
          std::to_string(spec.memory_mb) + " },\n";
  text += "  \"checker\": { \"kind\": \"" + spec.checker + "\" },\n";
  text += "  \"origin\": { \"kind\": \"ai\", \"pipeline\": \"cms_generate_task\" },\n";
  text += "  \"verification\": { \"status\": \"pending\", \"reference_solution\": "
          "\"solutions/main.cpp\", \"methods\": [] }\n";
  text += "}\n";
  return text;
}

auto write_file(const std::filesystem::path& path, std::string_view text) -> Result<void> {
  auto stream = std::ofstream{path};
  if (!stream) {
    return std::unexpected(Error::make(ErrorCode::unavailable, "cannot write " + path.string()));
  }
  stream << text;
  return {};
}

}  // namespace

auto generate_task(const TaskSpec& spec, const std::filesystem::path& directory) -> Result<void> {
  const auto complete = refuse_incomplete(spec);
  if (!complete) {
    return complete;
  }
  std::error_code exists_error;
  if (std::filesystem::exists(directory, exists_error)) {
    return std::unexpected(Error::make(ErrorCode::conflict,
                                       directory.string() + " already exists; a draft is never overwritten"));
  }

  std::error_code ignored;
  std::filesystem::create_directories(directory / "examples", ignored);
  std::filesystem::create_directories(directory / "solutions", ignored);

  const auto files = std::vector<std::pair<std::filesystem::path, std::string>>{
      {directory / "task.json", manifest_text(spec)},
      {directory / "statement.en.md", spec.statement_en + "\n"},
      {directory / "statement.th.md", spec.statement_th + "\n"},
      {directory / "examples" / "1.in", spec.example_input + "\n"},
      {directory / "examples" / "1.out", spec.example_output + "\n"},
      {directory / "solutions" / "main.cpp", spec.solution + "\n"},
  };
  for (const auto& [path, text] : files) {
    const auto written = write_file(path, text);
    if (!written) {
      return written;
    }
  }
  return {};
}

}  // namespace cms::judge
