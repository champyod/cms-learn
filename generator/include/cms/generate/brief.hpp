#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <cms/error.hpp>

namespace cms::generate {

/// @brief What step 1 receives: the fourteen authoring rows of ADR 0014, as the teacher set them.
///
/// A blank row is not a missing value. `0014` says the template fills it, so the planner is told which rows the
/// teacher left alone instead of being handed a default this code invented. `technique` and `difficulty` are
/// the two the form requires, and they are the two a run is refused without.
struct Brief {
  std::string technique;
  std::string difficulty;
  std::vector<std::string> topics;
  std::string io;
  std::string constraints;
  std::string answer_contract;
  std::string subtasks;
  std::string limits;
  std::string misdirection;
  std::string title;
  std::string description;
  std::vector<std::string> locales;
  std::string brief;
  std::string cast;
};

/// @brief Checks the two rows the authoring form requires.
/// @return Nothing when both are set, or one refusal naming every row that is blank.
[[nodiscard]] auto refuse_incomplete_brief(const Brief& brief) -> Result<void>;

/// @brief Renders the brief as the fact block a prompt carries, one labelled row per line, blanks kept.
[[nodiscard]] auto render_brief(const Brief& brief) -> std::string;

}  // namespace cms::generate
