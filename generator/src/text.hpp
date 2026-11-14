#pragma once

#include <cctype>
#include <string>
#include <string_view>

namespace cms::generate::detail {

/// @return @p text without the spaces, tabs and newlines at either end.
[[nodiscard]] inline auto trimmed(std::string_view text) -> std::string_view {
  const auto first = text.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) {
    return {};
  }
  const auto last = text.find_last_not_of(" \t\r\n");
  return text.substr(first, last - first + 1);
}

/// @return @p text lowercased with every run of whitespace collapsed, so two spellings of one approach compare
/// equal. The comparison decides whether four candidates are four plans or one plan written four times.
[[nodiscard]] inline auto normalised(std::string_view text) -> std::string {
  auto folded = std::string{};
  folded.reserve(text.size());
  auto spaced = false;
  for (const auto character : trimmed(text)) {
    if (std::isspace(static_cast<unsigned char>(character)) != 0) {
      spaced = true;
      continue;
    }
    if (spaced) {
      folded.push_back(' ');
      spaced = false;
    }
    folded.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
  }
  return folded;
}

}  // namespace cms::generate::detail
