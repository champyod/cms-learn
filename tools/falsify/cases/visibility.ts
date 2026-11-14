import type { Gate } from "../contract.ts";

/// The serving path, from the function that owns it down to the blank line before its predicates. The five lines
/// that make a task visible are written a second time further down the same file (`count_stage_tasks`), so a
/// pattern of the predicate alone matches twice and the plant cannot isolate the query this case is about. Starting
/// the pattern at the signature leaves one way to make it ambiguous again — a second `find_in_band` — and the plant
/// refuses that by name instead of patching whichever copy it met first.
const servingPathOwner = `auto find_in_band(db::connection& database, std::string_view account_id, const Stage& stage,
                  std::span<const std::int64_t> audience, std::int32_t minimum,
                  std::int32_t maximum) -> std::optional<NextTask> {
  const auto principals = std::vector<std::int64_t>(audience.begin(), audience.end());

`;

const visibilityPredicate = `  const auto is_visible = sqlpp::exists(
      sqlpp::select(db::taskVisibility.taskId)
          .from(db::taskVisibility)
          .where(db::taskVisibility.taskId == db::tasks.id &&
                 db::taskVisibility.principalId.in(principals)));`;

export const VISIBILITY_GATE: Gate = {
  id: "visibility",
  stage: ["bank", "content", "deploy", "docs/dependencies.md", "tools"],
  command: {
    command: "bash",
    args: [
      "tools/check-visibility.sh",
      "{repo}/build/safety/bank/cms_seed_content",
      "{repo}/build/safety/bank/cms_principal_value",
      "{repo}/build/safety",
    ],
  },
  cases: [
    {
      name: "visibility-predicate-removed",
      plant: [
        {
          kind: "patch",
          path: "bank/src/next_task.cpp",
          find: `${servingPathOwner}${visibilityPredicate}`,
          replace: `${servingPathOwner}  const auto is_visible = sqlpp::value(true);`,
        },
      ],
      expect: { status: "non-zero", message: "the serving query follows the audience" },
    },
  ],
};
