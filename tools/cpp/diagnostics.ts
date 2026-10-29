// Typed parser for compiler and clang-tidy diagnostics.
//
// Why text and not SARIF: GCC 16 does expose `-fdiagnostics-format=sarif-file`, but it derives the
// output name from the source path, rejects an explicit path
// (`-fdiagnostics-format=sarif-file=/x.sarif` → "unrecognized argument"), and writes it into the
// compiler's working directory. A parallel build with two `main.cpp` sources therefore cannot address
// its own reports. The text form is stable, line-oriented, and already carries the rule id in brackets —
// so it is parsed here with types instead of grepped in a shell.

export type Level = "error" | "warning" | "note";

export interface Finding {
  file: string;
  line: number;
  column: number;
  level: Level;
  message: string;
  /** `-Wunused-variable` for the compiler, `bugprone-use-after-move` for clang-tidy. */
  rule: string;
}

export interface ParseResult {
  findings: Finding[];
  /** Lines that declare a failure with no position: linker errors, `fatal error: ...`, collect2. */
  unlocated: string[];
}

const DIAGNOSTIC =
  /^(?<file>[^\s:][^:]*):(?<line>\d+)(?::(?<column>\d+))?:\s*(?<level>fatal error|error|warning|note):\s*(?<message>.*?)(?:\s*\[(?<rule>[-a-zA-Z0-9_.,=:]+)\])?$/;

// Failure shapes that carry no position. Deliberately a short allow-list rather than a search for the
// word "error": clang-tidy prints "…to display errors from all non-system headers" on every run, and a
// loose pattern turns that hint sentence into a fake gate failure.
const UNLOCATED = [
  /^(?:.*:\s*)?fatal error:/i,
  /^undefined reference to /,
  /^multiple definition of /,
  /^collect2: error:/,
  /^ld(?:\.\w+)?: error:/,
];

function parse(text: string): ParseResult {
  const findings: Finding[] = [];
  const unlocated: string[] = [];

  for (const raw of text.split("\n")) {
    const line = raw.trimEnd();
    if (line.length === 0) continue;

    const match = DIAGNOSTIC.exec(line);
    if (!match?.groups) {
      if (UNLOCATED.some((pattern) => pattern.test(line))) unlocated.push(line);
      continue;
    }

    const { file, line: at, column, level, message, rule } = match.groups;
    // GCC names a promoted warning `-Werror=unused-variable`; the rule people know is `-Wunused-variable`.
    // Normalising here keeps one spelling in reports and in the falsification expectations.
    const ruleId = (rule ?? "").split(",")[0] ?? "";
    findings.push({
      file,
      line: Number(at),
      column: Number(column ?? "0"),
      level: level === "fatal error" ? "error" : (level as Level),
      message,
      rule: ruleId.replace(/^-Werror=/, "-W"),
    });
  }

  return { findings, unlocated };
}

export const parseCompilerOutput = parse;
export const parseTidyOutput = parse;

/** The rule ids worth naming in a failure line, most severe first, deduplicated. */
export function rulesOf(result: ParseResult, limit = 3): string[] {
  const seen = new Set<string>();
  for (const finding of result.findings) {
    if (finding.level === "note") continue;
    seen.add(finding.rule === "" ? finding.message.slice(0, 60) : finding.rule);
  }
  for (const line of result.unlocated) seen.add(line.slice(0, 60));
  return [...seen].slice(0, limit);
}

export function hasFailure(result: ParseResult): boolean {
  return (
    result.unlocated.length > 0 ||
    result.findings.some((f) => f.level === "error" || f.level === "warning")
  );
}
