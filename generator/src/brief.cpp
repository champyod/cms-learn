#include <cms/generate/brief.hpp>

#include <string>
#include <string_view>
#include <vector>

#include "text.hpp"

namespace cms::generate {
namespace {

using detail::trimmed;

constexpr std::string_view BLANK_ROW = "(the template's)";

auto as_list(const std::vector<std::string>& values) -> std::string {
  auto joined = std::string{};
  for (const auto& value : values) {
    if (!joined.empty()) {
      joined += ", ";
    }
    joined += trimmed(value);
  }
  return joined.empty() ? std::string{BLANK_ROW} : joined;
}

auto row(std::string_view label, std::string_view value) -> std::string {
  const auto set = trimmed(value);
  return std::string{label} + ": " + (set.empty() ? std::string{BLANK_ROW} : std::string{set}) + "\n";
}

auto named(std::string& message, std::string_view name) -> void {
  if (!message.empty()) {
    message += ", ";
  }
  message += name;
}

}  // namespace

auto refuse_incomplete_brief(const Brief& brief) -> Result<void> {
  auto missing = std::string{};
  if (trimmed(brief.technique).empty()) {
    named(missing, "technique");
  }
  if (trimmed(brief.difficulty).empty()) {
    named(missing, "difficulty");
  }
  if (missing.empty()) {
    return {};
  }
  return std::unexpected(Error::make(ErrorCode::invalid_argument,
                                     "the brief is missing the rows the form requires: " + missing));
}

auto render_brief(const Brief& brief) -> std::string {
  auto block = row("technique", brief.technique);
  block += row("difficulty", brief.difficulty);
  block += "topics: " + as_list(brief.topics) + "\n";
  block += "locales: " + as_list(brief.locales) + "\n";
  block += row("io", brief.io);
  block += row("constraints", brief.constraints);
  block += row("answer contract", brief.answer_contract);
  block += row("subtasks", brief.subtasks);
  block += row("limits", brief.limits);
  block += row("misdirection", brief.misdirection);
  block += row("title", brief.title);
  block += row("description", brief.description);
  block += row("brief", brief.brief);
  block += row("cast", brief.cast);
  return block;
}

}  // namespace cms::generate
