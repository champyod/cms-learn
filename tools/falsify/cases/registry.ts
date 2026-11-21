import type { Gate } from "../contract.ts";

/// FEATURES.md is a promise list: a citation that does not resolve is a promise the platform cannot keep.
/// The cases plant the two defects the gate exists for — a row whose path is missing and a row whose ADR is
/// missing — plus one for the counts line, which must say what the rows say.
export const REGISTRY_GATE: Gate = {
  id: "registry",
  stage: ["FEATURES.md"],
  command: {
    command: "bun",
    args: ["run", "{repo}/tools/check-registry.ts", "--root", "{repo}", "--features", "{root}/FEATURES.md"],
  },
  cases: [
    {
      name: "control",
      expect: { status: "zero" },
    },
    {
      name: "a row citing a path that does not exist",
      plant: [
        {
          kind: "patch",
          path: "FEATURES.md",
          find: "`content/topics.json`",
          replace: "`content/topics.json`, `content/does-not-exist.json`",
        },
      ],
      expect: { status: "non-zero", message: "does not exist" },
    },
    {
      name: "a glob that matches no file",
      plant: [
        {
          kind: "patch",
          path: "FEATURES.md",
          find: "`content/tasks/*/examples/`",
          replace: "`content/tasks/*/no-such-directory/`",
        },
      ],
      expect: { status: "non-zero", message: "does not exist" },
    },
    {
      name: "a row citing an ADR that was never written",
      plant: [{ kind: "patch", path: "FEATURES.md", find: "ADR 0048", replace: "ADR 0099" }],
      expect: { status: "non-zero", message: "has no decision" },
    },
    {
      name: "a counts line that disagrees with the rows",
      plant: [
        {
          kind: "patch",
          path: "FEATURES.md",
          find: "Counts at last update: **52 shipped · 5 partial · 7 planned** (64 features).",
          replace: "Counts at last update: **51 shipped · 5 partial · 8 planned** (64 features).",
        },
      ],
      expect: { status: "non-zero", message: "the rows say" },
    },
  ],
};
