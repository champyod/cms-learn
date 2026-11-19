#include <cms/bank/scoring.hpp>

#include <cstdint>
#include <string>

namespace cms::bank {
namespace {

constexpr std::string_view THAI = "th";

auto heading(std::string_view locale) -> std::string {
  return locale == THAI ? "## \u0e01\u0e32\u0e23\u0e43\u0e2b\u0e49\u0e04\u0e30\u0e41\u0e19\u0e19\n\n"
                        : "## Scoring\n\n";
}

auto lead(std::string_view locale, std::int32_t total, std::size_t count) -> std::string {
  const auto points = std::to_string(total);
  const auto subtasks = std::to_string(count);
  if (locale == THAI) {
    return "\u0e07\u0e32\u0e19\u0e19\u0e35\u0e49\u0e21\u0e35\u0e17\u0e31\u0e49\u0e07\u0e2b\u0e21\u0e14 " +
           points + " \u0e04\u0e30\u0e41\u0e19\u0e19 \u0e41\u0e1a\u0e48\u0e07\u0e40\u0e1b\u0e47\u0e19 " +
           subtasks +
           " \u0e07\u0e32\u0e19\u0e22\u0e48\u0e2d\u0e22 "
           "\u0e04\u0e30\u0e41\u0e19\u0e19\u0e02\u0e2d\u0e07\u0e07\u0e32\u0e19\u0e22\u0e48\u0e2d\u0e22"
           "\u0e08\u0e30\u0e44\u0e14\u0e49\u0e40\u0e21\u0e37\u0e48\u0e2d\u0e1c\u0e48\u0e32\u0e19"
           "\u0e17\u0e38\u0e01\u0e40\u0e04\u0e2a\u0e43\u0e19\u0e01\u0e25\u0e38\u0e48\u0e21\u0e19\u0e31\u0e49"
           "\u0e19\n\n" +
           "| \u0e07\u0e32\u0e19\u0e22\u0e48\u0e2d\u0e22 | \u0e40\u0e07\u0e37\u0e48\u0e2d\u0e19\u0e44\u0e02 | "
           "\u0e04\u0e30\u0e41\u0e19\u0e19 |\n| --- | --- | --- |\n";
  }
  return "The task is worth " + points + " points in " + subtasks +
         " subtasks. A subtask's points are awarded when every test in its group passes.\n\n"
         "| Subtask | Constraint | Points |\n| --- | --- | --- |\n";
}

}  // namespace

auto scoring_section(std::string_view locale, const std::vector<Subtask>& subtasks,
                     std::int32_t total) -> std::string {
  if (subtasks.empty()) {
    return {};
  }
  auto text = heading(locale);
  text += lead(locale, total, subtasks.size());
  for (const auto& subtask : subtasks) {
    text += "| " + std::to_string(subtask.position) + " | " + cms::bounds_text(subtask.bounds) + " | " +
            std::to_string(subtask.points) + " |\n";
  }
  return text;
}

}  // namespace cms::bank
