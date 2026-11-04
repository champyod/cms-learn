import type { Gate } from "../contract.ts";

const RANGE_TASK = "content/tasks/range-sum-query/task.json";
const GRID_TASK = "content/tasks/grid-shortest-path/task.json";
const TRACKS = "content/tracks.json";
const PREREQS = "content/prereqs.json";

export const CONTENT_GATE: Gate = {
  id: "content",
  stage: ["content", "tools", "package.json"],
  link: ["node_modules"],
  command: { command: "bun", args: ["run", "tools/validate-content.ts"] },
  cases: [
    {
      name: "baseline bank",
      expect: { status: "zero" },
    },
    {
      name: "published task without verification",
      plant: [{ kind: "json", path: RANGE_TASK, edits: [{ kind: "set", pointer: ["status"], value: "published" }] }],
      expect: { status: "non-zero" },
    },
    {
      name: "task tagged outside the vocabulary",
      plant: [{ kind: "json", path: GRID_TASK, edits: [{ kind: "append", pointer: ["topics"], value: "graph-theory" }] }],
      expect: { status: "non-zero" },
    },
    {
      name: "stage names a tag outside the vocabulary",
      plant: [
        {
          kind: "json",
          path: TRACKS,
          edits: [{ kind: "append", pointer: ["tracks", 0, "stages", 0, "tags"], value: "tag-that-does-not-exist" }],
        },
      ],
      expect: { status: "non-zero" },
    },
    {
      name: "tag repeated inside one stage",
      plant: [
        {
          kind: "json",
          path: TRACKS,
          edits: [
            {
              kind: "appendCopy",
              pointer: ["tracks", 0, "stages", 0, "tags"],
              source: ["tracks", 0, "stages", 0, "tags", 0],
            },
          ],
        },
      ],
      expect: { status: "non-zero" },
    },
    {
      name: "missing statement locale",
      plant: [{ kind: "remove", path: "content/tasks/range-sum-query/statement.th.md" }],
      expect: { status: "non-zero" },
    },
    {
      name: "empty task bank",
      plant: [{ kind: "remove", path: "content/tasks" }],
      expect: { status: "non-zero" },
    },
    {
      name: "stage lists no tag",
      plant: [{ kind: "json", path: TRACKS, edits: [{ kind: "set", pointer: ["tracks", 0, "stages", 0, "tags"], value: [] }] }],
      expect: { status: "non-zero" },
    },
    {
      name: "tag placed by no stage",
      plant: [
        {
          kind: "json",
          path: TRACKS,
          edits: [{ kind: "removeValue", pointer: ["tracks", "*", "stages", "*", "tags"], value: "simulation" }],
        },
      ],
      expect: { status: "non-zero" },
    },
    {
      name: "tag missing from one route",
      plant: [
        {
          kind: "json",
          path: TRACKS,
          edits: [
            {
              kind: "removeValue",
              pointer: ["tracks", { match: "id", equals: "t-usaco" }, "stages", "*", "tags"],
              value: "simulation",
            },
          ],
        },
      ],
      expect: { status: "non-zero" },
    },
    {
      name: "label with no language",
      plant: [{ kind: "json", path: TRACKS, edits: [{ kind: "set", pointer: ["tracks", 0, "stages", 0, "title"], value: {} }] }],
      expect: { status: "non-zero" },
    },
    {
      name: "inverted difficulty band",
      plant: [
        {
          kind: "json",
          path: TRACKS,
          edits: [{ kind: "set", pointer: ["tracks", 0, "stages", 1, "difficulty"], value: { min: 1600, max: 1100 } }],
        },
      ],
      expect: { status: "non-zero" },
    },
    {
      name: "task unreachable by every stage",
      plant: [{ kind: "json", path: GRID_TASK, edits: [{ kind: "set", pointer: ["difficulty"], value: 3500 }] }],
      expect: { status: "non-zero" },
    },
    {
      name: "prerequisite outside the vocabulary",
      plant: [
        {
          kind: "json",
          path: PREREQS,
          edits: [{ kind: "append", pointer: ["edges", 0, "prereqs"], value: "tag-that-does-not-exist" }],
        },
      ],
      expect: { status: "non-zero" },
    },
    {
      name: "prerequisite cycle",
      plant: [
        {
          kind: "json",
          path: PREREQS,
          edits: [
            {
              kind: "append",
              pointer: ["edges", { match: "tag", equals: "sorting" }, "prereqs"],
              value: "segment-tree",
            },
          ],
        },
      ],
      expect: { status: "non-zero" },
    },
  ],
};
