#pragma once

#include <cstddef>
#include <string_view>

namespace cms {

namespace detail {

/// @return Whether @p character separates tokens: the whitespace the statement's layout may carry between values.
[[nodiscard]] inline auto is_token_space(char character) -> bool {
  return character == ' ' || character == '\t' || character == '\n' || character == '\r' ||
         character == '\v' || character == '\f';
}

/// @return The next token of @p text from @p cursor, advancing @p cursor past it; empty at the end.
[[nodiscard]] inline auto next_token(std::string_view text, std::size_t& cursor) -> std::string_view {
  while (cursor < text.size() && is_token_space(text[cursor])) {
    ++cursor;
  }
  const auto first = cursor;
  while (cursor < text.size() && !is_token_space(text[cursor])) {
    ++cursor;
  }
  return text.substr(first, cursor - first);
}

}  // namespace detail

/// @brief The one rule for "the two answers agree": the same token sequence, the layout ignored (ADR 0042).
///
/// A token is a maximal run of non-whitespace, so a trailing space, a line break, or two spaces where one was are
/// not part of the answer. An answer with a token missing, added, or changed disagrees. A program that exits non-zero
/// never reaches this rule: that is a refusal to answer, not a different answer.
///
/// The judge's own token checker applies the same rule to a submission, and `judge/tests/token_checker_test.sh`
/// cross-checks the two so they cannot drift.
[[nodiscard]] inline auto tokens_equal(std::string_view expected, std::string_view answer) -> bool {
  auto left = std::size_t{0};
  auto right = std::size_t{0};
  while (true) {
    const auto left_token = detail::next_token(expected, left);
    const auto right_token = detail::next_token(answer, right);
    if (left_token.empty() || right_token.empty()) {
      return left_token.empty() && right_token.empty();
    }
    if (left_token != right_token) {
      return false;
    }
  }
}

}  // namespace cms
