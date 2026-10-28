// C++ gate: the one command that decides whether the C++ half of this repo is safe to ship.
//
// What it owns vs what it delegates — the split matters, and it is the reason this is TypeScript at all:
//   CMake presets own the flags     → the binary the gate tests is the binary the build produces
//   ctest owns pass/fail per case   → "it printed the right thing" is a test, not a shell comparison
//   clang-tidy owns static analysis → classes of defect ASan/UBSan cannot see (they only see what runs)
//   this file owns reporting        → typed findings, one line of output, a reason when it fails
//
// It never decides whether C++ is correct; it reads what the toolchain said and refuses to soften it.

import { spawnSync } from "node:child_process";
import { existsSync, readFileSync, readdirSync, statSync } from "node:fs";
import { join, relative } from "node:path";

import {
  hasFailure,
  parseCompilerOutput,
  parseTidyOutput,
  rulesOf,
  type Finding,
} from "./diagnostics.ts";

export interface GateReport {
  ok: boolean;
  /** Reference executables the build produced. */
  targets: number;
  /** ctest cases that ran and passed. */
  cases: number;
  /** Translation units clang-tidy analysed. */
  analysed: number;
  findings: Finding[];
  /** Human-readable reasons, in the order they were discovered. Empty when ok. */
  reasons: string[];
}

interface RunResult {
  status: number;
  output: string;
}

function run(command: string, args: string[], cwd: string, env?: Record<string, string>): RunResult {
  const result = spawnSync(command, args, {
    cwd,
    encoding: "utf8",
    env: { ...process.env, ...env },
    maxBuffer: 32 * 1024 * 1024,
  });
  return {
    status: result.status ?? 127,
    // cmake writes build output to stderr; both streams belong to the same report.
    output: `${result.stdout ?? ""}${result.stderr ?? ""}`,
  };
}

/** Every `.cpp` this repo ships, found on disk rather than trusted from a manifest. */
export function cppSources(root: string): string[] {
  const found: string[] = [];
  const walk = (dir: string): void => {
    if (!existsSync(dir)) return;
    for (const entry of readdirSync(dir, { withFileTypes: true })) {
      const path = join(dir, entry.name);
      if (entry.isDirectory()) {
        if (entry.name === "build" || entry.name === "node_modules") continue;
        walk(path);
      } else if (entry.name.endsWith(".cpp")) {
        found.push(path);
      }
    }
  };
  walk(join(root, "content/tasks"));
  walk(join(root, "cpp"));
  walk(join(root, "api"));
  walk(join(root, "judge"));
  return found.sort();
}

function countTests(root: string): number {
  const listed = run("ctest", ["--preset", "safety", "-N"], join(root, "cpp"));
  const total = /Total Tests:\s*(\d+)/.exec(listed.output);
  return total ? Number(total[1]) : 0;
}

export function runGate(root: string, options: { tidy?: boolean } = {}): GateReport {
  const tidy = options.tidy ?? true;
  const report: GateReport = {
    ok: false,
    targets: 0,
    cases: 0,
    analysed: 0,
    findings: [],
    reasons: [],
  };

  if (cppSources(root).length === 0) {
    report.reasons.push("no C++ source found — an empty run is not a pass");
    return report;
  }

  const cpp = join(root, "cpp");

  // 1. Configure. Presets, not ad-hoc flags: this is where the sanitizer and warning set come from.
  const configured = run("cmake", ["--preset", "safety"], cpp);
  if (configured.status !== 0) {
    report.findings.push(...parseCompilerOutput(configured.output).findings);
    report.reasons.push(`configure failed: ${rulesOf(parseCompilerOutput(configured.output)).join(", ") || "see output"}`);
    return report;
  }

  // 2. Build under ASan + UBSan + -Werror.
  const built = run("cmake", ["--build", "--preset", "safety"], cpp);
  const buildDiagnostics = parseCompilerOutput(built.output);
  report.findings.push(...buildDiagnostics.findings);
  if (built.status !== 0 || hasFailure(buildDiagnostics)) {
    report.reasons.push(`build failed: ${rulesOf(buildDiagnostics).join(", ") || "see output"}`);
    return report;
  }
  report.targets = (built.output.match(/Built target /g) ?? []).length;

  // 3. Run the cases. The examples are the specification; ctest is what turns them into a verdict.
  report.cases = countTests(root);
  const tested = run("ctest", ["--preset", "safety"], cpp);
  if (tested.status !== 0) {
    const failed = [...tested.output.matchAll(/^\s*\d+\s+-\s+(\S+)/gm)].map((m) => m[1]);
    const detail = failed.length > 0 ? failed.slice(0, 3).join(", ") : "see output";
    report.reasons.push(`ctest failed${failed.length > 3 ? ` (+${failed.length - 3} more)` : ""}: ${detail}`);
    // A sanitizer report on stdout/stderr of the test is not in the ctest summary — surface it, with its
    // detail: "runtime error:" alone names nothing, while "signed integer overflow" names the defect.
    const crash = /(ERROR: (?:Address|Leak)Sanitizer[^\n]*|runtime error: [^\n]*|stack-buffer-overflow[^\n]*)/.exec(
      tested.output,
    );
    if (crash) report.reasons.push(`sanitizer: ${crash[0].trim()}`);
    return report;
  }

  if (!tidy) {
    report.ok = true;
    return report;
  }

  // 4. Static analysis over the compile database the build just produced.
  const tidyConfigured = run("cmake", ["--preset", "tidy"], cpp);
  const database = join(root, "build/tidy/compile_commands.json");
  if (tidyConfigured.status !== 0 || !existsSync(database)) {
    report.reasons.push("tidy configure produced no compile_commands.json");
    return report;
  }

  const units = new Set<string>();
  for (const entry of JSON.parse(readFileSync(database, "utf8")) as { file: string }[]) {
    if (entry.file.startsWith(root) && entry.file.endsWith(".cpp") && !entry.file.includes("/build/")) {
      units.add(entry.file);
    }
  }
  if (units.size === 0) {
    report.reasons.push("clang-tidy had no translation unit to analyse");
    return report;
  }

  // --config-file is explicit: the config lives in cpp/, while the sources live under content/tasks/,
  // and clang-tidy only walks up from the file — without this flag the curated check set silently
  // never applies and the gate reports a clean sweep it never performed.
  // No --warnings-as-errors on the command line: clang-tidy 23 takes it as an *optional argument*, so a
  // bare flag swallows the first source path and reports "no input files specified" — which then parses
  // as if it were a diagnostic. Promotion belongs in cpp/.clang-tidy (`WarningsAsErrors: '*'`), where it
  // is visible and applies the same way to a human running the tool by hand.
  const tidyRun = run(
    "clang-tidy",
    ["-p", join(root, "build/tidy"), "--config-file", join(cpp, ".clang-tidy"), ...units],
    cpp,
  );
  const tidyDiagnostics = parseTidyOutput(tidyRun.output);
  report.findings.push(...tidyDiagnostics.findings);
  report.analysed = units.size;
  if (tidyRun.status !== 0 || hasFailure(tidyDiagnostics)) {
    const rules = rulesOf(tidyDiagnostics);
    // A non-zero exit with no parsed diagnostic means the tool itself failed (usage, config, crash) —
    // report its own line rather than inventing a rule name.
    const lastLine = tidyRun.output.split("\n").map((l) => l.trim()).filter(Boolean).at(-1) ?? "";
    report.reasons.push(`clang-tidy: ${rules.join(", ") || lastLine.slice(0, 140) || `exit ${tidyRun.status}`}`);
    return report;
  }

  report.ok = true;
  return report;
}

function format(report: GateReport, root: string): string {
  const parts = [
    `${report.targets} target(s) built`,
    `${report.cases} case(s) passed`,
    `${report.analysed} unit(s) analysed`,
    report.ok ? "0 finding(s)" : `${report.findings.length} finding(s)`,
  ];
  const head = `cpp gate — ${parts.join(" · ")}`;
  if (report.ok) return head;

  const lines = [head];
  for (const reason of report.reasons) lines.push(`  FAIL ${reason}`);
  for (const finding of report.findings.filter((f) => f.level !== "note").slice(0, 5)) {
    lines.push(`       ${relative(root, finding.file)}:${finding.line}:${finding.column} ${finding.rule || finding.message}`);
  }
  return lines.join("\n");
}

if (import.meta.main) {
  const root = process.argv[2] ?? join(import.meta.dir, "../..");
  const report = runGate(root);
  console.log(format(report, root));
  process.exit(report.ok ? 0 : 1);
}
