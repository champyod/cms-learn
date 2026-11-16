#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <cms/bank/bank.hpp>

namespace cms::bank {

/// @brief The scoring section a statement carries: one row per subtask, generated from the rows the judge scores by.
///
/// The section is not typed into a statement. It is rendered from the subtask rows, so what a learner reads and what
/// the judge awards cannot drift apart (ADR 0033 §4).
///
/// @param locale The statement's locale; a locale this section has no wording for reads in English, as every page does.
/// @param subtasks The task's scoring groups, in position order.
/// @param total What the task is scored out of.
/// @return The markdown section, newline-terminated; empty when the task has no subtasks.
[[nodiscard]] auto scoring_section(std::string_view locale, const std::vector<Subtask>& subtasks,
                                   std::int32_t total) -> std::string;

}  // namespace cms::bank
