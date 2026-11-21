// Falsification for the C++ gate: every planted defect must be caught, and caught *by name*.
//
// A gate that fails for the wrong reason is as broken as one that passes a defect — the deploy harness
// once reported "6 caught, 1 missed" while actually catching a YAML syntax error instead of judge egress.
// So each case asserts the rule id (or the sanitizer report) that must appear, not merely a non-zero exit.
//
// Each case is built in its own staged tree: the source tree is staged, a defect is written into
// `content/tasks/demo/solutions/`, and the *real* gate runs against it. Nothing in the repo is mutated.
//
// The trees live under a scratch root this file owns — `CMS_FALSIFY_SCRATCH`, else
// `$XDG_CACHE_HOME/cms-learn/falsify` — never in the system temp directory: nine staged trees and their
// sanitizer builds are several gigabytes, so a tmpfs scratch root holds all of it in RAM, and a run that
// starts with too little room then fails a different case every time. The root is swept of the trees a
// killed run abandoned before anything is staged, and a run below the free-space floor refuses to start.
//
// A gate that fails without naming a rule prints "see output", which its caller never sees: this file
// re-runs that one step itself so the log carries the first `CMake Error` line instead.

import { spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import {
  closeSync,
  cpSync,
  existsSync,
  mkdirSync,
  openSync,
  readFileSync,
  readdirSync,
  rmSync,
  statSync,
  statfsSync,
  symlinkSync,
  writeFileSync,
} from "node:fs";
import { homedir } from "node:os";
import { join } from "node:path";

import { format, runGate } from "./gate.ts";

interface Case {
  name: string;
  source: string;
  /** Substring that must appear in the failure — a rule id or a sanitizer report. */
  expect: string;
  /** Feed the program an example so ctest runs it; a build-time defect needs no examples. */
  examples?: { input: string; output: string };
  /** Only static analysis can see this defect, so tidy must be part of the run. */
  staticOnly?: boolean;
}

const CASES: Case[] = [
  {
    name: "use-after-free",
    expect: "AddressSanitizer",
    examples: { input: "3\n", output: "3\n" },
    source: `#include <cstdio>
int main() {
    int *value = new int(42);
    delete value;
    std::printf("%d\\n", *value);
    return 0;
}`,
  },
  {
    name: "signed-overflow",
    expect: "signed integer overflow",
    examples: { input: "3\n", output: "3\n" },
    source: `#include <climits>
#include <cstdio>
int main() {
    int value = INT_MAX;
    value += 1;
    std::printf("%d\\n", value);
    return 0;
}`,
  },
  {
    name: "stack-buffer-overflow",
    expect: "stack-buffer-overflow",
    examples: { input: "3\n", output: "3\n" },
    source: `#include <cstdio>
int main() {
    int values[3] = {1, 2, 3};
    int *slot = values;
    int index = 7;
    slot[index] = 42;
    std::printf("%d\\n", values[0]);
    return 0;
}`,
  },
  {
    name: "unused-variable",
    expect: "-Wunused-variable",
    source: `#include <cstdio>
int main() {
    int unused = 7;
    std::printf("ok\\n");
    return 0;
}`,
  },
  {
    name: "use-after-move",
    expect: "bugprone-use-after-move",
    examples: { input: "3\n", output: "0\n" },
    staticOnly: true,
    // The program is *correct* by the example (prints 0, exits 0) and clean under both sanitizers — only
    // static analysis can see that `first` is read after being moved from.
    source: `#include <cstdio>
#include <string>
#include <utility>
int main() {
    std::string first = "abc";
    std::string second = std::move(first);
    std::printf("%d\\n", first.empty() ? 0 : 1);
    return second.empty() ? 1 : 0;
}`,
  },
];

const CLEAN_SOURCE = `#include <cstdio>
int main() {
    int value = 0;
    if (std::scanf("%d", &value) != 1) return 1;
    std::printf("%d\\n", value);
    return 0;
}`;

const CLEAN_EXAMPLES = { input: "3\n", output: "3\n" };

// Everything one configure reads: the presets and the tidy config, the CMake modules, the components the
// root CMakeLists adds, and the manifest the presets resolve packages from.
// The files a configure reads, plus every component the root CMakeLists adds, plus the data directories
// those components read at run time. All three were hardcoded once and drifted: `bank/` was added to the
// root CMakeLists and not to this list, so every staged tree died at configure, and `content/` and
// `deploy/catalogue/` were missing, so thirty-three data-backed tests failed for want of their files.
const STAGED_CONFIG = ["CMakeLists.txt", "CMakePresets.json", ".clang-tidy", "cmake", "vcpkg-ports"];

/**
 * The data a staged tree carries besides its sources.
 *
 * The staging rule: a staged tree must hold every directory the cases it runs reach for, because a case that cannot find
 * one either excludes itself or fails for the missing directory instead of for the defect it exists to catch — an
 * unanswered question wearing a pass. So the list is the full set of static data the suite reads: the content the gates
 * load, the deployment the deploy cases parse, and the tools the e2e cases invoke by path. A new case that needs a new
 * directory adds it here, and `stagedEntries` refuses to stage a tree when a listed directory is missing.
 */
const STAGED_DATA = ["content", "deploy", "tools"];

function stagedEntries(repo: string): string[] {
  const rootLists = join(repo, "CMakeLists.txt");
  const root = readFileSync(rootLists, "utf8");
  const components = [...root.matchAll(/^\s*add_subdirectory\(([A-Za-z0-9_]+)\)/gm)].map((match) => match[1]);
  if (components.length === 0) {
    throw new Error(`no add_subdirectory(...) found in ${rootLists}; a staged tree would not configure`);
  }
  for (const entry of [...components, ...STAGED_DATA]) {
    if (!existsSync(join(repo, entry))) {
      throw new Error(`${rootLists} and the test data expect ${entry}/ but that directory is not in the tree`);
    }
  }
  return [...STAGED_CONFIG, ...components, ...STAGED_DATA];
}

/**
 * Writes the demo task a case runs: its reference solution, its manifest, its statements and its examples.
 *
 * The document is written on every call, and the examples directory is emptied first, so a case that supplies
 * no examples cannot inherit the previous case's — the same rule the tree reset below enforces for the source.
 */
function writeDemo(root: string, source: string, examples?: { input: string; output: string }): void {
  const demo = join(root, "content/tasks/demo");
  const solutions = join(demo, "solutions");
  mkdirSync(solutions, { recursive: true });
  writeFileSync(join(solutions, "main.cpp"), `${source}\n`);
  writeFileSync(
    join(demo, "task.json"),
    `${JSON.stringify(
      {
        id: "demo",
        version: 1,
        status: "draft",
        title: { en: "Falsification probe", th: "งานตรวจสอบ" },
        topics: ["complexity"],
        difficulty: 800,
        locales: ["en", "th"],
        limits: { time_ms: 1000, memory_mb: 256 },
        checker: { kind: "token" },
        origin: { kind: "original" },
        verification: { status: "pending", reference_solution: "solutions/main.cpp", methods: [] },
      },
      null,
      2,
    )}\n`,
  );
  writeFileSync(join(demo, "statement.en.md"), "Read one integer and print it.\n");
  writeFileSync(join(demo, "statement.th.md"), "อ่านจำนวนเต็มหนึ่งจำนวนแล้วพิมพ์ออกมา\n");
  rmSync(join(demo, "examples"), { recursive: true, force: true });
  if (examples) {
    const dir = join(demo, "examples");
    mkdirSync(dir, { recursive: true });
    writeFileSync(join(dir, "01.in"), examples.input);
    writeFileSync(join(dir, "01.out"), examples.output);
  }
}

/** Everything a staged tree is compiled from: the component sources, the data, the presets and the modules.
 * The checkout path is part of the digest too: a tree's cache and package links name absolute paths, so two
 * checkouts never share one. */
function stagedTreeDigest(repo: string): string {
  const hash = createHash("sha256");
  hash.update("cms-learn-cpp-falsify-tree-v1\n");
  hash.update(`repo ${repo}\n`);
  const hashPath = (path: string, relative: string): void => {
    if (statSync(path).isDirectory()) {
      hash.update(`d ${relative}\n`);
      for (const entry of readdirSync(path).sort()) hashPath(join(path, entry), `${relative}/${entry}`);
    } else {
      hash.update(`f ${relative}\n`);
      hash.update(readFileSync(path));
    }
  };
  for (const entry of stagedEntries(repo).sort()) hashPath(join(repo, entry), entry);
  return hash.digest("hex");
}

/** Copies the source and configuration a configure reads, and links the installed packages every tree compiles against. */
function stageTree(repo: string, root: string): void {
  for (const entry of stagedEntries(repo)) {
    cpSync(join(repo, entry), join(root, entry), {
      recursive: true,
      // build/ holds absolute paths into the original tree; copying it would make CMake reuse them.
      filter: (path) => !path.includes("/build"),
    });
  }

  linkInstalled(repo, root);
}

/** Links the installed packages a tree compiles against; re-pointed on reuse so a checkout that moved cannot
 * leave a dangling link. */
function linkInstalled(repo: string, root: string): void {
  const installed = join(repo, "build/vcpkg_installed");
  if (!existsSync(installed)) return;
  mkdirSync(join(root, "build"), { recursive: true });
  const link = join(root, "build/vcpkg_installed");
  rmSync(link, { force: true });
  symlinkSync(installed, link, "dir");
}

const STAGED_MARKER = ".staged";

/**
 * Stages the tree this source digest names, or reuses the one an earlier run staged and built.
 *
 * The path is the digest, so a second run in the same checkout state lands on the same directory and its
 * build products — and the compiler cache keyed by that path — are reused; a changed source or preset gets a
 * different directory instead of a stale build. A tree without the marker is one a killed run left half
 * copied, and is staged again.
 *
 * @return true when this run staged it, false when it reused an earlier run's tree.
 */
function stageOrReuse(repo: string, root: string, digest: string): boolean {
  const marker = join(root, STAGED_MARKER);
  if (existsSync(marker) && readFileSync(marker, "utf8").trim() === digest) {
    linkInstalled(repo, root);
    return false;
  }
  rmSync(root, { recursive: true, force: true });
  stageTree(repo, root);
  writeFileSync(marker, `${digest}\n`);
  return true;
}

function buildTree(repo: string, work: string, name: string, source: string, examples?: { input: string; output: string }): string {
  const root = join(work, name);
  stageTree(repo, root);
  writeDemo(root, source, examples);
  return root;
}

function reasonText(report: ReturnType<typeof runGate>): string {
  const rules = report.findings.map((f) => f.rule).filter(Boolean);
  return [...report.reasons, ...rules].join(" | ");
}

/** A configure a planted cache value must stop — the gate only reports configures that got past it. */
function configureWith(root: string, cache: Record<string, string>): { status: number; output: string } {
  const overrides = Object.entries(cache).map(([key, value]) => `-D${key}=${value}`);
  const result = spawnSync("cmake", ["--preset", "safety", ...overrides], {
    cwd: root,
    encoding: "utf8",
    env: { ...process.env },
    maxBuffer: 32 * 1024 * 1024,
  });
  return { status: result.status ?? 127, output: `${result.stdout ?? ""}${result.stderr ?? ""}` };
}

/** The version the preset's compiler reports, which a refusal has to name. */
function gnuCompilerVersion(): string {
  const result = spawnSync("g++", ["-dumpfullversion"], { encoding: "utf8" });
  return (result.stdout ?? "").trim();
}

const TREE_PREFIX = "tree-";
const LEGACY_PREFIX = "cms-learn-cpp-falsify-";
const SCRATCH_ROOT =
  process.env.CMS_FALSIFY_SCRATCH ?? join(process.env.XDG_CACHE_HOME ?? join(homedir(), ".cache"), "cms-learn", "falsify");

// One staged tree plus its safety build measures 0.65 GiB on this repository. A run keeps one tree, its
// three probe trees and the tidy pass; the floor is that plus headroom, and it is checked before the first
// byte is staged.
const REQUIRED_BYTES = 8 * 1024 ** 3;

function formatGib(bytes: number): string {
  return `${(bytes / 1024 ** 3).toFixed(1)} GiB`;
}

/** Free bytes for unprivileged writes on the filesystem holding `path`. */
function freeBytes(path: string): number {
  const stats = statfsSync(path);
  return stats.bavail * stats.bsize;
}

const LOCK_NAME = "run.lock";

/** The run that created the lock file, read back to decide whether it is still alive. */
interface LockHolder {
  pid: number;
  startedAt: string;
}

function readHolder(text: string): LockHolder | null {
  const [pid, startedAt] = text.trim().split("\n");
  const value = Number.parseInt(pid ?? "", 10);
  if (!Number.isInteger(value) || value <= 0) return null;
  return { pid: value, startedAt: startedAt === undefined || startedAt === "" ? "unknown" : startedAt };
}

/** Signal 0 runs the existence and permission checks without delivering anything to the process. */
function isRunning(pid: number): boolean {
  try {
    process.kill(pid, 0);
    return true;
  } catch (error) {
    // EPERM means the process exists under another user; only ESRCH proves that it is gone.
    return (error as NodeJS.ErrnoException).code !== "ESRCH";
  }
}

/** The holder recorded in `path`, or null when the file cannot be read — an unreadable lock is never stale. */
function lockHolder(path: string): LockHolder | null {
  try {
    return readHolder(readFileSync(path, "utf8"));
  } catch {
    // The lock can be released between the failed create and this read, leaving nothing to report.
    return null;
  }
}

function refuseBusy(path: string, holder: LockHolder | null): never {
  const who = holder === null ? "a run whose lock file could not be read" : `pid ${holder.pid} since ${holder.startedAt}`;
  console.error(`scratch root busy: ${path} is held by ${who}`);
  process.exit(1);
}

/** Creates the lock exclusively, so the first run to reach it wins and every later one sees EEXIST. */
function takeLock(path: string): boolean {
  try {
    closeSync(openSync(path, "wx"));
    return true;
  } catch (error) {
    if ((error as NodeJS.ErrnoException).code !== "EEXIST") throw error;
    return false;
  }
}

/**
 * Claims the scratch root for this run, or exits naming the run that holds it, and returns the release.
 *
 * A lock left by a process that no longer exists is removed and the acquisition retried once; a lock whose
 * holder cannot be identified is left alone, because only a dead pid proves that a run has finished.
 */
function acquireLock(root: string): () => void {
  const path = join(root, LOCK_NAME);
  if (!takeLock(path)) {
    const holder = lockHolder(path);
    if (holder === null || isRunning(holder.pid)) refuseBusy(path, holder);
    rmSync(path, { force: true });
    console.log(`cleared a stale lock at ${path} — pid ${holder.pid} is not running`);
    if (!takeLock(path)) refuseBusy(path, lockHolder(path));
  }

  writeFileSync(path, `${process.pid}\n${new Date().toISOString()}\n`);
  // Only the finally on the case loop releases the lock, so a killed run leaves the file behind and the next
  // run clears it through the stale-pid check above. Nothing else may release it: this script never yields to
  // the event loop, so a signal handler would not run until the whole run had finished, and registering one
  // would suppress the default kill in the meantime.
  return (): void => rmSync(path, { force: true });
}

/** A killed run leaves its tree behind; every tree but the one this run reuses is stale. */
function sweepOrphans(root: string, keep: string): void {
  for (const entry of readdirSync(root, { withFileTypes: true })) {
    if (!entry.isDirectory()) continue;
    const stale = entry.name.startsWith(LEGACY_PREFIX) || (entry.name.startsWith(TREE_PREFIX) && join(root, entry.name) !== keep);
    if (stale) rmSync(join(root, entry.name), { recursive: true, force: true });
  }
}

function refuseBelowFloor(root: string): void {
  const free = freeBytes(root);
  if (free >= REQUIRED_BYTES) return;
  console.error(
    `not enough scratch space: ${root} has ${formatGib(free)} free, ${formatGib(REQUIRED_BYTES)} required for nine staged trees and their builds`,
  );
  process.exit(1);
}

function streamLines(stream: string | null): string[] {
  return `${stream ?? ""}`
    .split("\n")
    .map((line) => line.trim())
    .filter(Boolean);
}

/** The line a "see output" reason swallowed: the first `CMake Error`, else the first stderr line. */
function firstErrorLine(command: string, args: string[], root: string): string {
  const result = spawnSync(command, args, {
    cwd: root,
    encoding: "utf8",
    env: { ...process.env },
    maxBuffer: 32 * 1024 * 1024,
  });
  const stderr = streamLines(result.stderr);
  return stderr.find((line) => line.startsWith("CMake Error")) ?? stderr[0] ?? streamLines(result.stdout)[0] ?? "";
}

const FAILED_STEP = [
  { reason: "configure failed", command: "cmake", args: ["--preset", "safety"] },
  { reason: "build failed", command: "cmake", args: ["--build", "--preset", "safety"] },
  { reason: "ctest failed", command: "ctest", args: ["--preset", "safety"] },
];

/** Re-runs the step that failed without naming a rule, so the reader sees its cause instead of a pointer. */
function causeOf(report: ReturnType<typeof runGate>, root: string): string {
  const text = reasonText(report);
  if (!text.includes("see output")) return text;
  const step = FAILED_STEP.find((entry) => report.reasons.some((reason) => reason.startsWith(entry.reason)));
  if (step === undefined) return text;
  const cause = firstErrorLine(step.command, step.args, root);
  return cause === "" ? text : text.replace("see output", `see output — ${cause.slice(0, 200)}`);
}

const BASELINE_STEPS = [
  { what: "configure", command: "cmake", args: ["--preset", "safety"] },
  { what: "build", command: "cmake", args: ["--build", "--preset", "safety"] },
];

/** ctest reads the `-E` value as a regular expression, so every metacharacter in a case name is escaped. */
function escapeForCtest(name: string): string {
  return name.replace(/[.+?()\[\]{}|^$\\]/g, "\\$&");
}

/** A case ctest says it failed; a skipped case has its own section and is not a failure of the tree. */
const FAILED_CASE = /^\s*\d+\s+-\s+(.+?)\s+\((?:Failed|Timeout|Exception)\)\s*$/gm;

/** One command in a staged tree, both streams kept: cmake reports on stderr and ctest splits the two. */
function runIn(root: string, command: string, args: string[]): { status: number; output: string } {
  const result = spawnSync(command, args, {
    cwd: root,
    encoding: "utf8",
    env: { ...process.env },
    maxBuffer: 64 * 1024 * 1024,
  });
  return { status: result.status ?? 127, output: `${result.stdout ?? ""}${result.stderr ?? ""}` };
}

/** A baseline that cannot be built leaves no tree to judge, so the run stops and names the step that stopped it. */
function stopBaseline(work: string, what: string, output: string): never {
  const tail = streamLines(output).slice(-30).join("\n");
  console.error(`the baseline tree does not ${what}, so this run has no staged tree to judge:\n${tail}`);
  rmSync(work, { recursive: true, force: true });
  releaseLock();
  process.exit(1);
}

/**
 * Names the cases that fail in a tree built from the clean source, and returns them as one ctest exclusion.
 *
 * A staged tree carries the source, the presets and the data a configure and a run read, but not the bank
 * the image ships, so the cases that assert that bank fail there for want of their data rather than from a
 * planted defect — and the gate returns before static analysis on any ctest failure, which would turn the
 * control and the one static-only case into wrong reasons. The names are read from this run instead of
 * being written down here, because a hand-kept list is the drift that left `bank/` out of the staging.
 *
 * Returns "" when the baseline is already green: a staged tree that needs no exclusion gets none.
 */
/** The launcher the configure recorded, or "" when the tree was configured without a compiler cache. */
function scImapLauncher(root: string): string {
  const cache = join(root, "build", "safety", "CMakeCache.txt");
  if (!existsSync(cache)) return "";
  const found = /CMAKE_CXX_COMPILER_LAUNCHER:[^=]*=([^\n]*)/.exec(readFileSync(cache, "utf8"));
  return found === null ? "" : found[1].trim();
}

function baselineExclusion(root: string, work: string): string {
  for (const step of BASELINE_STEPS) {
    const result = runIn(root, step.command, step.args);
    if (result.status !== 0) stopBaseline(work, step.what, result.output);
  }
  const launcher = scImapLauncher(root);
  if (launcher !== "" && !existsSync(launcher)) {
    console.error(
      `the staged tree is configured to use the compiler cache at ${launcher}, which does not exist. Point the ` +
        "launcher at a working ccache or configure without one (docs/dependencies.md, section 'Compiler cache').",
    );
    releaseLock();
    process.exit(1);
  }

  const tested = runIn(root, "ctest", ["--preset", "safety"]);
  const failed = [...new Set([...tested.output.matchAll(FAILED_CASE)].map((match) => match[1].trim()))];
  // A non-zero status with nothing named is a ctest that could not run its cases, not a tree with failures.
  if (tested.status !== 0 && failed.length === 0) stopBaseline(work, "run its cases", tested.output);

  if (failed.length === 0) {
    console.log("baseline      0 test(s) fail in a staged tree — every case runs the full suite");
    return "";
  }
  console.log(
    `baseline      ${failed.length} test(s) fail in a staged tree and are excluded by name — this run does not prove: ${failed.join(", ")}` +
      `\n              excluded because a staged tree carries no database and no docker socket, so a case that needs either\n` +
      `              cannot state a verdict — each is named above rather than counted as a pass`,
  );
  return failed.map(escapeForCtest).join("|");
}

interface CacheStats {
  /** Hits since the cache was created. */
  hits: number;
  /** Cacheable compiles since the cache was created; its delta is this run's compile count. */
  calls: number;
}

/** The cache totals a run subtracts from to state what this run hit; null when ccache is absent. */
function cacheStats(): CacheStats | null {
  const result = spawnSync("ccache", ["--show-stats"], { encoding: "utf8", env: process.env });
  if (result.status !== 0) return null;
  const hits = /^\s*Hits:\s*(\d+)\s*\/\s*(\d+)/m.exec(result.stdout ?? "");
  // A cache that has never compiled prints no Hits line; that is zero calls, not missing stats.
  return hits === null ? { hits: 0, calls: 0 } : { hits: Number(hits[1]), calls: Number(hits[2]) };
}

const repo = join(import.meta.dir, "../..");
mkdirSync(SCRATCH_ROOT, { recursive: true });
// Every staged tree is compiled from the same source except for the file a case plants, so the work is the same
// work repeated. A compiler cache keyed by the preprocessed source and the exact flags makes each tree after the
// first cost one link plus one file — and the key is why a hit can never carry a defect from another tree.
const CACHE_DIR = join(SCRATCH_ROOT, "ccache");
mkdirSync(CACHE_DIR, { recursive: true });
process.env.CCACHE_DIR = CACHE_DIR;
process.env.CCACHE_BASEDIR = SCRATCH_ROOT;
process.env.CCACHE_COMPILERCHECK = "content";
// A cache is an optimisation, never a requirement: a machine without ccache builds the same tree once and takes
// the same time as before it existed. Only a launcher that is configured and missing is a refusal, because that is
// a broken environment rather than a plain one.
const cacheAvailable = spawnSync("ccache", ["--version"], { encoding: "utf8" }).status === 0;
if (!cacheAvailable) {
  delete process.env.CCACHE_DIR;
  delete process.env.CCACHE_BASEDIR;
  delete process.env.CCACHE_COMPILERCHECK;
}

const startedAt = Date.now();

// The tree a run builds is named by the source and configuration it is built from: the same checkout in the same
// state lands on the same directory, so a second run reuses the first run's build and the compiler cache entries
// keyed by that path. A changed source gets its own tree rather than a stale one, and the planted defect is
// rebuilt per case below, so one case's build cannot decide another case's verdict.
const digest = stagedTreeDigest(repo);
const tree = join(SCRATCH_ROOT, `${TREE_PREFIX}${digest.slice(0, 16)}`);

// The sweep is keyed on a shared directory prefix, so without a lock one run deletes the tree another run is
// still building. Two full safety builds at once exhaust the machine's memory long before they finish.
const releaseLock = acquireLock(SCRATCH_ROOT);
sweepOrphans(SCRATCH_ROOT, tree);
refuseBelowFloor(SCRATCH_ROOT);

const cacheBefore = cacheStats();
const staged = stageOrReuse(repo, tree, digest);
console.log(`tree          ${staged ? "staged" : "reused"} at ${tree}`);
writeDemo(tree, CLEAN_SOURCE, CLEAN_EXAMPLES);
// The probe trees are configure-time and empty-tree checks; they are rebuilt every run, so a reused tree
// cannot carry one of them from a previous run.
for (const name of ["empty-tree", "compiler-minimum", "compiler-family"]) {
  rmSync(join(tree, name), { recursive: true, force: true });
}

// What a staged tree cannot prove is read from this run rather than written down here, and nothing inherited
// from the caller survives: this run derives its own exclusion, and an unset one leaves the gate unchanged.
delete process.env.CMS_CTEST_EXCLUDE;
// The tree is built once, its ctest names the cases a staged tree cannot prove, and the planted cases reuse
// the build instead of paying for it again.
const exclusion = baselineExclusion(tree, tree);
if (exclusion !== "") process.env.CMS_CTEST_EXCLUDE = exclusion;

let caught = 0;
let missed = 0;

try {
  // A single case can be run for diagnosis; the gate never sets this, so a real run is every case.
  const only = (process.env.CMS_FALSIFY_ONLY ?? "").split(",").filter(Boolean);
  for (const testCase of CASES.filter((entry) => only.length === 0 || only.includes(entry.name))) {
    const caseStarted = Date.now();
    writeDemo(tree, testCase.source, testCase.examples);
    const root = tree;
    // The defect lives in the demo reference, which is a build product: it must be rebuilt for this case, and the
    // binary's mtime proves it rather than the build system's word for it.
    const reference = join(root, "build/safety/references/ref_demo_main");
    const planted = join(root, "content/tasks/demo/solutions/main.cpp");
    const rebuilt = runIn(root, "cmake", ["--build", "--preset", "safety", "--target", "ref_demo_main"]);
    // A build-time defect is meant to fail this rebuild, and the gate reports that failure as the defect; the
    // freshness check therefore only applies when the reference did build.
    const fresh = existsSync(reference) && statSync(reference).mtimeMs >= statSync(planted).mtimeMs;
    if (rebuilt.status === 0 && !fresh) {
      console.log(
        `WRONG RESULT  ${testCase.name.padEnd(28)} — the planted reference is older than its source, so this case ` +
          "would be judged against another case's source",
      );
      missed += 1;
      continue;
    }
    // tidy only where the defect is invisible to the sanitizers: it is the slow half, and running it on
    // a case that already fails at build time proves nothing about static analysis.
    const report = runGate(root, { tidy: testCase.staticOnly === true });
    const text = causeOf(report, root);

    const seconds = ((Date.now() - caseStarted) / 1000).toFixed(1);
    if (!report.ok && text.includes(testCase.expect)) {
      console.log(`caught        ${testCase.name.padEnd(28)} ${seconds.padStart(6)}s (${testCase.expect})`);
      caught += 1;
    } else if (report.ok) {
      console.log(`MISSED        ${testCase.name.padEnd(28)} ${seconds.padStart(6)}s — gate passed a planted defect`);
      missed += 1;
    } else {
      console.log(
        `WRONG REASON  ${testCase.name.padEnd(28)} ${seconds.padStart(6)}s — expected "${testCase.expect}", got: ${text.slice(0, 160)}`,
      );
      missed += 1;
    }
  }

  // The control: a correct solution must pass, otherwise the gate is failing everything and proves
  // nothing about the defects above.
  writeDemo(tree, CLEAN_SOURCE, CLEAN_EXAMPLES);
  const cleanRoot = tree;
  const clean = runGate(cleanRoot);
  if (clean.ok) {
    console.log(`behaved       ${"clean-control".padEnd(28)} (a correct solution passes)`);
    caught += 1;
  } else {
    console.log(`WRONG RESULT  ${"clean-control".padEnd(28)} — correct solution rejected: ${causeOf(clean, cleanRoot).slice(0, 160)}`);
    missed += 1;
  }

  // An empty tree must not be green: "nothing to compile" is unplugged, not passing.
  const emptyRoot = join(tree, "empty-tree");
  mkdirSync(join(emptyRoot, "content/tasks"), { recursive: true });
  const empty = runGate(emptyRoot);
  if (!empty.ok && reasonText(empty).includes("no C++ source")) {
    console.log(`behaved       ${"empty-tree".padEnd(28)} (refuses an empty run)`);
    caught += 1;
  } else {
    console.log(`WRONG RESULT  ${"empty-tree".padEnd(28)} — reported green with no sources`);
    missed += 1;
  }

  // A run that stops before the analysis must say so. "0 unit(s) analysed … 0 finding(s)" is a true count that forms a
  // false sentence — it reads as an analysis that found nothing when it never ran — so the summary has to name the step
  // it did not reach. The empty tree stops the gate before any unit is analysed, which is the shape of the failure.
  const stoppedSummary = format(empty, emptyRoot);
  const stoppedNamed = stoppedSummary.includes("static analysis did not run") && !stoppedSummary.includes("0 unit(s) analysed");
  if (stoppedNamed) {
    console.log(`behaved       ${"stopped-analysis".padEnd(28)} (names the analysis it never ran)`);
    caught += 1;
  } else {
    console.log(`WRONG RESULT  ${"stopped-analysis".padEnd(28)} — the summary reads as a clean analysis: ${stoppedSummary.split("\n")[0].slice(0, 140)}`);
    missed += 1;
  }

  // And a run that does analyse every unit still prints its count, so the honest case is unchanged by the line above.
  const analysedSummary = format(clean, cleanRoot);
  const honestCount = /\d+ unit\(s\) analysed in \d+ worker\(s\)/.test(analysedSummary) && !analysedSummary.includes("did not run");
  if (honestCount) {
    console.log(`behaved       ${"analysed-summary".padEnd(28)} (prints the count it measured)`);
    caught += 1;
  } else {
    console.log(`WRONG RESULT  ${"analysed-summary".padEnd(28)} — the honest summary changed: ${analysedSummary.split("\n")[0].slice(0, 140)}`);
    missed += 1;
  }

  // The compiler refusal is a configure-time defect: raise the required major above the installed compiler
  // and configure must stop naming both numbers. A check that never fires leaves the configure green, so
  // this case fails on that instead of passing for the wrong reason.
  const minimumRoot = buildTree(repo, tree, "compiler-minimum", CLEAN_SOURCE);
  const refusal = configureWith(minimumRoot, { CMS_MIN_GCC_MAJOR: "99" });
  const installed = gnuCompilerVersion();
  const named = refusal.status !== 0 && installed !== "" && refusal.output.includes(installed) && refusal.output.includes("99");
  if (named) {
    console.log(`behaved       ${"compiler-minimum".padEnd(28)} (refuses a compiler below the minimum)`);
    caught += 1;
  } else {
    const why = refusal.status === 0 ? "configure accepted a compiler below the minimum" : `refusal did not name "${installed}" and 99`;
    console.log(`WRONG RESULT  ${"compiler-minimum".padEnd(28)} — ${why}`);
    missed += 1;
  }

  // A Clang major is not a GCC major: clang++ 23 is numerically above 16 and still accepts -std=c++26 while
  // compiling a smaller language, so a family test on the version floor passes it for the wrong reason. The
  // preset configures with g++, so the override is also what proves the check reads the compiler that
  // actually configured rather than the one the preset asked for.
  const clangRoot = buildTree(repo, tree, "compiler-family", CLEAN_SOURCE);
  const clangRefusal = configureWith(clangRoot, { CMAKE_CXX_COMPILER: "clang++" });
  const clangNamed =
    clangRefusal.status !== 0 &&
    clangRefusal.output.includes("Clang") &&
    clangRefusal.output.includes("required minimum '16'");
  if (clangNamed) {
    console.log(`behaved       ${"compiler-family".padEnd(28)} (refuses a Clang major that is not a GCC major)`);
    caught += 1;
  } else {
    const detail = clangRefusal.output.trim().split("\n").at(-1)?.slice(0, 140) ?? "";
    const why =
      clangRefusal.status === 0
        ? "configure accepted clang++ on the numeric match alone"
        : `refusal did not name "Clang" and the required minimum: ${detail}`;
    console.log(`WRONG RESULT  ${"compiler-family".padEnd(28)} — ${why}`);
    missed += 1;
  }
} finally {
  releaseLock();
}

const totalSeconds = ((Date.now() - startedAt) / 1000).toFixed(1);
const cacheAfter = cacheStats();
const cacheLine =
  cacheBefore === null || cacheAfter === null
    ? "\ncache         stats unavailable"
    : `\ncache         ${cacheAfter.hits - cacheBefore.hits} hit(s) of ${cacheAfter.calls - cacheBefore.calls} compile(s) this run (served from ${CACHE_DIR})`;
console.log(cacheLine);
console.log(`\ntotal         ${totalSeconds}s for ${caught + missed} case(s) on a ${staged ? "staged" : "reused"} tree`);
console.log(`\n${caught} case(s) behaved, ${missed} did not`);
process.exit(missed === 0 ? 0 : 1);
