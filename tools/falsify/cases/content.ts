import type { Gate } from "../contract.ts";

const RANGE_TASK = "content/tasks/range-sum-query/task.json";
const GRID_TASK = "content/tasks/grid-shortest-path/task.json";
const TRACKS = "content/tracks.json";
const PREREQS = "content/prereqs.json";
const LEVELS = "content/levels.json";

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
      name: "technique outside the task's topics",
      plant: [{ kind: "json", path: GRID_TASK, edits: [{ kind: "set", pointer: ["technique"], value: "dijkstra" }] }],
      expect: { status: "non-zero" },
    },
    {
      name: "a gap between two level bands",
      plant: [{ kind: "json", path: LEVELS, edits: [{ kind: "set", pointer: ["levels", 2, "min"], value: 2000 }] }],
      expect: { status: "non-zero" },
    },
    {
      name: "two level bands overlapping",
      plant: [{ kind: "json", path: LEVELS, edits: [{ kind: "set", pointer: ["levels", 1, "max"], value: 1900 }] }],
      expect: { status: "non-zero" },
    },
    {
      name: "level bands that stop short of the scale",
      plant: [{ kind: "json", path: LEVELS, edits: [{ kind: "set", pointer: ["levels", 3, "max"], value: 3400 }] }],
      expect: { status: "non-zero" },
    },
    {
      name: "level band inverted",
      plant: [{ kind: "json", path: LEVELS, edits: [{ kind: "set", pointer: ["levels", 1, "max"], value: 1200 }] }],
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
      name: "a task carrying two real subtasks",
      plant: [
        {
          kind: "patch",
          path: RANGE_TASK,
          find: `"limits": { "time_ms": 1000, "memory_mb": 256 },`,
          replace: `"limits": { "time_ms": 1000, "memory_mb": 256 },
  "bounds": { "min_n": 1, "max_n": 200000, "value_min": -1000000000, "value_max": 1000000000 },
  "subtasks": [
    { "position": 1, "points": 40, "max_n": 2000 },
    { "position": 2, "points": 60, "max_n": 200000 }
  ],`,
        },
      ],
      expect: { status: "zero" },
    },
    {
      name: "a subtask that narrows nothing",
      plant: [
        {
          kind: "patch",
          path: RANGE_TASK,
          find: `"limits": { "time_ms": 1000, "memory_mb": 256 },`,
          replace: `"limits": { "time_ms": 1000, "memory_mb": 256 },
  "bounds": { "min_n": 1, "max_n": 200000, "value_min": -1000000000, "value_max": 1000000000 },
  "subtasks": [{ "position": 1, "points": 100 }],`,
        },
      ],
      expect: { status: "non-zero", message: "narrows nothing" },
    },
    {
      name: "a subtask wider than the task's bounds",
      plant: [
        {
          kind: "patch",
          path: RANGE_TASK,
          find: `"limits": { "time_ms": 1000, "memory_mb": 256 },`,
          replace: `"limits": { "time_ms": 1000, "memory_mb": 256 },
  "bounds": { "min_n": 1, "max_n": 200000, "value_min": -1000000000, "value_max": 1000000000 },
  "subtasks": [{ "position": 1, "points": 100, "max_n": 200001 }],`,
        },
      ],
      expect: { status: "non-zero", message: "above the task's 200000" },
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
