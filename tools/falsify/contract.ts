export type JsonValue = null | boolean | number | string | JsonValue[] | { [key: string]: JsonValue };

/** `*` fans out over every element of an array or every value of an object. */
export type JsonPath = readonly (string | number | "*" | { match: string; equals: string })[];

export type JsonEdit =
  | { kind: "set"; pointer: JsonPath; value: JsonValue }
  | { kind: "append"; pointer: JsonPath; value: JsonValue }
  | { kind: "appendCopy"; pointer: JsonPath; source: JsonPath }
  | { kind: "removeValue"; pointer: JsonPath; value: JsonValue };

export type Plant =
  | { kind: "write"; path: string; content: string }
  | { kind: "append"; path: string; content: string }
  | { kind: "patch"; path: string; find: string; replace: string }
  | { kind: "replaceAll"; path: string; find: string; replace: string; matches: number }
  | { kind: "remove"; path: string }
  | { kind: "json"; path: string; edits: readonly JsonEdit[] }
  | { kind: "run"; script: string };

/** `cwd` defaults to the staged root; `{root}` and `{repo}` in `args`, `cwd` and `env` values are replaced, and no other placeholder exists. */
export interface GateCommand {
  command: string;
  args: readonly string[];
  env?: Readonly<Record<string, string>>;
  cwd?: string;
}

export interface FalsifyCase {
  name: string;
  plant?: readonly Plant[];
  command?: GateCommand;
  expect: { status: "zero" } | { status: "non-zero"; message?: string; messageMatch?: "case-insensitive" };
}

/** `stage` entries are copied into each case tree, `link` entries symlinked; `command` is the one the cases run. */
export interface Gate {
  id: string;
  stage: readonly string[];
  link?: readonly string[];
  command: GateCommand;
  cases: readonly FalsifyCase[];
}
