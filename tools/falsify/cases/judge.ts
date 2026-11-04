import type { Gate } from "../contract.ts";

export const JUDGE_GATE: Gate = {
  id: "judge",
  stage: ["content", "deploy", "judge", "tools"],
  command: {
    command: "bash",
    args: [
      "tools/check-judge.sh",
      "{repo}/build/safety/judge/cms_judge_run",
      "{repo}/build/safety/judge/cms_checker_token",
      "{repo}/build/safety/bank/cms_seed_content",
    ],
  },
  cases: [
    {
      name: "wrong-answer-accepted",
      plant: [
        {
          kind: "patch",
          path: "judge/tests/fixtures/wrong-answer.cpp",
          find: "  std::cout << 0 << '\\n';",
          replace: "  std::cout << 8 << '\\n' << 11 << '\\n';",
        },
      ],
      expect: { status: "non-zero", message: "wrong-answer" },
    },
  ],
};
