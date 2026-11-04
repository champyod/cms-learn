import type { Gate } from "../contract.ts";

const predicate = `  const auto is_visible = sqlpp::exists(
      sqlpp::select(db::taskVisibility.taskId)
          .from(db::taskVisibility)
          .where(db::taskVisibility.taskId == db::tasks.id &&
                 db::taskVisibility.principalId.in(principals)));`;

export const VISIBILITY_GATE: Gate = {
  id: "visibility",
  stage: ["bank", "content", "deploy", "tools"],
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
          find: predicate,
          replace: "  const auto is_visible = sqlpp::value(true);",
        },
      ],
      expect: { status: "non-zero", message: "the serving query follows the audience" },
    },
  ],
};
