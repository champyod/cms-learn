import type { Gate } from "../contract.ts";

export const PM_GATE: Gate = {
  id: "pm",
  stage: ["package.json", "Makefile", "bun.lock", "tools"],
  command: { command: "bash", args: ["tools/check-pm.sh"] },
  cases: [
    {
      name: "control",
      expect: { status: "zero" },
    },
    {
      name: "lockfile-planted",
      plant: [{ kind: "write", path: "package-lock.json", content: "{}\n" }],
      expect: { status: "non-zero", message: "expected exactly one lockfile at the root" },
    },
    {
      name: "script-invokes-npm",
      plant: [{ kind: "append", path: "tools/check-db.sh", content: "\nnpm install left-pad\n" }],
      expect: { status: "non-zero", message: "another package manager is invoked" },
    },
  ],
};
