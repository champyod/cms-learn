import type { Gate } from "../contract.ts";

/// The one rule that decides what a learner may see, exactly as `bank/src/next_task.cpp` writes it. The serving
/// query and the stage's count both read this definition, so a defect planted here is what the gate's live cases
/// have to catch — and the pattern cannot be satisfied by a copy of the rule elsewhere, because there is no copy.
const visibilityRule = `auto is_visible_to(const std::vector<std::int64_t>& principals) {
  return sqlpp::exists(
      sqlpp::select(db::taskVisibility.taskId)
          .from(db::taskVisibility)
          .where(db::taskVisibility.taskId == db::tasks.id &&
                 db::taskVisibility.principalId.in(principals)));
}`;

/// The rule removed: the parameter loses its name as well, because the tree compiles with -Wunused-parameter and
/// -Werror, and a build failure would be the gate failing for a reason this case does not name.
const visibilityRuleRemoved = `auto is_visible_to(const std::vector<std::int64_t>&) {
  return sqlpp::value(true);
}`;

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
          find: visibilityRule,
          replace: visibilityRuleRemoved,
        },
      ],
      expect: { status: "non-zero", message: "the serving query follows the audience" },
    },
  ],
};
