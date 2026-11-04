import type { Gate } from "../contract.ts";

export const VISIBILITY_GATE: Gate = {
  id: "visibility",
  stage: ["content", "deploy", "tools"],
  command: {
    command: "bash",
    args: [
      "tools/check-visibility.sh",
      "{repo}/build/safety/bank/cms_seed_content",
      "{repo}/build/safety/bank/cms_principal_value",
    ],
  },
  cases: [
    {
      name: "hidden-task-served",
      plant: [
        {
          kind: "patch",
          path: "tools/check-visibility.sh",
          find: 'visibility_filter="EXISTS (SELECT 1 FROM task_visibility AS v WHERE v.task_id = t.id AND v.principal_id = ANY(ARRAY[$everyone, $user7, $team9]::bigint[]))"',
          replace: 'visibility_filter="TRUE"',
        },
      ],
      expect: { status: "non-zero", message: "hidden-task" },
    },
  ],
};
