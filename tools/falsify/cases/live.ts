import type { Gate } from "../contract.ts";

export const LIVE_GATE: Gate = {
  id: "live",
  stage: ["deploy", "tools"],
  command: {
    command: "bash",
    args: ["tools/check-live.sh", "{repo}/build/safety"],
  },
  cases: [
    {
      name: "tag-selects-nothing",
      plant: [
        {
          kind: "patch",
          path: "tools/check-live.sh",
          find: 'live_tag="live"',
          replace: 'live_tag="no-such-live-tag"',
        },
      ],
      expect: { status: "non-zero", message: "selected 0" },
    },
  ],
};
