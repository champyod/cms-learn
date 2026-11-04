import type { Gate } from "../contract.ts";

export const REFERENCES_GATE: Gate = {
  id: "references",
  stage: ["content", "tools"],
  command: { command: "bash", args: ["tools/check-references.sh"] },
  cases: [
    {
      name: "honest references",
      expect: { status: "zero" },
    },
    {
      name: "expected output that the reference disagrees with",
      plant: [{ kind: "write", path: "content/tasks/range-sum-query/examples/1.out", content: "8\n12\n" }],
      expect: { status: "non-zero" },
    },
    {
      name: "task with no examples to prove against",
      plant: [{ kind: "remove", path: "content/tasks/grid-shortest-path/examples" }],
      expect: { status: "non-zero" },
    },
    {
      name: "reference that does not compile",
      plant: [{ kind: "write", path: "content/tasks/grid-shortest-path/solutions/main.cpp", content: "int main() { this is not c++ }\n" }],
      expect: { status: "non-zero" },
    },
    {
      name: "no tasks at all",
      plant: [{ kind: "remove", path: "content/tasks" }],
      expect: { status: "non-zero" },
    },
  ],
};
