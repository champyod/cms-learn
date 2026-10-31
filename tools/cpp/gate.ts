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
import { existsSync, mkdirSync, readFileSync, readdirSync, statSync } from "node:fs";
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
  /** ctest cases that did not run, by name — a skip is never counted as a pass. */
  skipped: string[];
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
  walk(join(root, "core"));
  walk(join(root, "references"));
  walk(join(root, "api"));
  walk(join(root, "judge"));
  return found.sort();
}

interface TestOutcome {
  /** Cases ctest registered in this build tree. */
  total: number;
  /** Cases that did not run, with the status ctest gave them. */
  notRun: { name: string; status: string }[];
}

const NOT_RUN_STATUSES = new Set(["Skipped", "Disabled", "Not Run"]);

// ctest names the cases it did not run in a trailing section and marks each of them in the per-case
// listing; both are read because a disabled case appears only in the listing.
const NOT_RUN_LINE = /^\s*\d+ - (.+?) \((\w[\w ]*)\)[ \t]*$/gm;
const NOT_RUN_CASE_LINE = /^\s*\d+\/\d+ Test\s+#\d+:\s+(.+?)[ \t.]+\*\*\*([\w][\w ]*)/gm;

function readTestOutcome(output: string): TestOutcome {
  const total = Number(/out of (\d+)/.exec(output)?.[1] ?? 0);
  const notRun = new Map<string, string>();

  const summaryAt = output.indexOf("The following tests did not run:");
  if (summaryAt >= 0) {
    for (const match of output.slice(summaryAt).matchAll(NOT_RUN_LINE)) {
      notRun.set(match[1], match[2]);
    }
  }
  for (const match of output.matchAll(NOT_RUN_CASE_LINE)) {
    if (NOT_RUN_STATUSES.has(match[2])) notRun.set(match[1], match[2]);
  }

  return { total, notRun: [...notRun].map(([name, status]) => ({ name, status })) };
}

/** The test binaries a configured test directory has to leave behind. */
const TEST_SUITES = [
  { suite: "core/tests", binary: "cms_core_tests" },
  { suite: "db/tests", binary: "cms_db_tests" },
  { suite: "db/tests", binary: "cms_db_connection_tests" },
];

function missingSuites(root: string): string {
  const build = join(root, "build/safety");
  const absent = TEST_SUITES.filter((entry) => !existsSync(join(build, entry.suite, entry.binary))).map(
    (entry) => `${entry.suite} (${entry.binary})`,
  );
  return absent.length > 0 ? `no test binary under ${absent.join(", ")}` : "the test suites registered no case";
}

/** The live database is the only dependency a case is allowed to be missing. */
function isLiveDatabaseConfigured(): boolean {
  return (process.env.CMSLEAN_TEST_DSN ?? "") !== "";
}

/** Components db/CMakeLists.txt adds only when the vcpkg tree supplies sqlpp23 and Drogon. */
const DATABASE_COMPONENTS = ["cms::db", "cms::db_drogon"];

interface ComponentAudit {
  /** Components the configure never added, although the vcpkg tree is present. */
  missing: string[];
  /** False when the configure left no trace to read, so the components cannot be confirmed. */
  traced: boolean;
}

/** The same tree tools/check-scaffold.sh treats as "dependencies installed". */
function hasInstalledDependencies(root: string): boolean {
  return ["build/vcpkg_installed", "vcpkg_installed"].some((prefix) => {
    const dir = join(root, prefix);
    if (!existsSync(dir)) return false;
    return readdirSync(dir, { withFileTypes: true }).some(
      (entry) => entry.isDirectory() && existsSync(join(dir, entry.name, "include")),
    );
  });
}

function auditDatabaseComponents(root: string, traceFile: string): ComponentAudit {
  if (!hasInstalledDependencies(root)) return { missing: [], traced: true };
  if (!existsSync(traceFile)) return { missing: [], traced: false };
  const trace = readFileSync(traceFile, "utf8");
  return {
    missing: DATABASE_COMPONENTS.filter((name) => !trace.includes(`add_library(${name} `)),
    traced: true,
  };
}

export function runGate(root: string, options: { tidy?: boolean } = {}): GateReport {
  const tidy = options.tidy ?? true;
  const report: GateReport = {
    ok: false,
    targets: 0,
    cases: 0,
    skipped: [],
    analysed: 0,
    findings: [],
    reasons: [],
  };

  if (cppSources(root).length === 0) {
    report.reasons.push("no C++ source found — an empty run is not a pass");
    return report;
  }

  const cpp = root;
  const safety = join(root, "build/safety");
  mkdirSync(safety, { recursive: true });
  const traceFile = join(safety, "configure-trace.txt");

  // 1. Configure. Presets, not ad-hoc flags: this is where the sanitizer and warning set come from.
  //    The trace is how a component that a found dependency should have added is confirmed, so a
  //    dependency the tree has but the configure dropped cannot hide behind a green build.
  const configured = run("cmake", ["--preset", "safety", `--trace-redirect=${traceFile}`], cpp);
  if (configured.status !== 0) {
    report.findings.push(...parseCompilerOutput(configured.output).findings);
    report.reasons.push(`configure failed: ${rulesOf(parseCompilerOutput(configured.output)).join(", ") || "see output"}`);
    return report;
  }

  const components = auditDatabaseComponents(root, traceFile);
  if (!components.traced) {
    report.reasons.push("the configure wrote no trace, so the database components cannot be confirmed");
    return report;
  }
  if (components.missing.length > 0) {
    report.reasons.push(
      `database component(s) missing from the configure although the vcpkg tree is present: ${components.missing.join(", ")}`,
    );
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
  const tested = run("ctest", ["--preset", "safety"], cpp);
  const outcome = readTestOutcome(tested.output);
  report.cases = Math.max(outcome.total - outcome.notRun.length, 0);
  report.skipped = outcome.notRun.map((entry) => entry.name);

  // Catch2 is looked up at configure time, so its absence leaves a green build with no case at all.
  if (outcome.total === 0) {
    report.reasons.push(`no ctest case ran — ${missingSuites(root)}; an empty run is not a pass`);
    return report;
  }

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

  // The live database is the one dependency a case may be missing; anything else that stops a case
  // from running — a disabled case, a guard that fired with the database configured — is a failure.
  const liveDatabase = isLiveDatabaseConfigured();
  const unexplained = outcome.notRun.filter((entry) => entry.status !== "Skipped" || liveDatabase);
  if (unexplained.length > 0) {
    const names = unexplained.map((entry) => `${entry.name} (${entry.status})`).join(", ");
    report.reasons.push(
      liveDatabase
        ? `case(s) skipped although CMSLEAN_TEST_DSN is set: ${names}`
        : `case(s) disabled rather than run: ${names}`,
    );
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

  // --config-file is explicit: the config lives at the repository root, while the sources live under content/tasks/,
  // and clang-tidy only walks up from the file — without this flag the curated check set silently
  // never applies and the gate reports a clean sweep it never performed.
  // No --warnings-as-errors on the command line: clang-tidy 23 takes it as an *optional argument*, so a
  // bare flag swallows the first source path and reports "no input files specified" — which then parses
  // as if it were a diagnostic. Promotion belongs in .clang-tidy (`WarningsAsErrors: '*'`), where it
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

/** The version line of a tool, or "<tool> missing" — a gate that cannot say what it ran is unprovable. */
function toolVersion(root: string, command: string[]): string {
  const result = run(command[0], command.slice(1), root);
  // gcc answers "g++ (GCC) 16.1.1 …", clang "Ubuntu clang version 18.1.3", clang-tidy puts "LLVM
  // version 23.1.1" on its second line — so take the first line that actually carries a version number.
  const numbered = result.output
    .split("\n")
    .map((line) => line.trim())
    .filter((line) => /\d+\.\d+/.test(line));
  return (numbered[0] ?? `${command[0]} missing`).slice(0, 80);
}

function format(report: GateReport, root: string): string {
  const parts = [
    `${report.targets} target(s) built`,
    `${report.cases} case(s) passed`,
    `${report.analysed} unit(s) analysed`,
    report.ok ? "0 finding(s)" : `${report.findings.length} finding(s)`,
  ];
  // Which tools produced the verdict is part of the verdict: clang-tidy's check set grows between major
  // versions, so "green on 23" and "green on 18" are different claims. Printing the versions makes that
  // difference visible in the log instead of mysterious.
  const versions = `${toolVersion(root, ["g++", "--version"])} / ${toolVersion(root, ["clang++", "--version"])} / ${toolVersion(root, ["clang-tidy", "--version"])}`;
  // A skipped case is printed by name on every run: a partial run that looks exactly like a full one is
  // how a live suite stops being exercised without anyone noticing.
  const skip =
    report.skipped.length === 0
      ? []
      : [
          `           skipped: ${report.skipped.join(", ")}${isLiveDatabaseConfigured() ? "" : " (CMSLEAN_TEST_DSN is not set)"}`,
        ];
  const head = `cpp gate — ${parts.join(" · ")}\n${[...skip, `           ${versions}`].join("\n")}`;
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
