// Falsification for the C++ gate: every planted defect must be caught, and caught *by name*.
//
// A gate that fails for the wrong reason is as broken as one that passes a defect — the deploy harness
// once reported "6 caught, 1 missed" while actually catching a YAML syntax error instead of judge egress.
// So each case asserts the rule id (or the sanitizer report) that must appear, not merely a non-zero exit.
//
// Each case is built in its own temp tree: the source tree is staged, a defect is written into
// `content/tasks/demo/solutions/`, and the *real* gate runs against it. Nothing in the repo is mutated.

import { spawnSync } from "node:child_process";
import { cpSync, existsSync, mkdirSync, mkdtempSync, rmSync, symlinkSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

import { runGate } from "./gate.ts";

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
    int cells[3] = {1, 2, 3};
    cells[7] = 9;
    std::printf("%d\\n", cells[0]);
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

// Everything one configure reads: the presets and the tidy config, the CMake modules, the components the
// root CMakeLists adds, and the manifest the presets resolve packages from.
const STAGED = [
  "CMakeLists.txt",
  "CMakePresets.json",
  ".clang-tidy",
  "cmake",
  "core",
  "db",
  "references",
  "api",
  "vcpkg-ports",
];

function buildTree(repo: string, work: string, name: string, source: string, examples?: { input: string; output: string }): string {
  const root = join(work, name);
  for (const entry of STAGED) {
    cpSync(join(repo, entry), join(root, entry), {
      recursive: true,
      // build/ holds absolute paths into the original tree; copying it would make CMake reuse them.
      filter: (path) => !path.includes("/build"),
    });
  }

  // The root CMakeLists adds every component, and the presets resolve packages from the manifest, so a
  // tree without them dies at configure and the case never reaches a compile. The installed packages are
  // shared by link rather than copied: the manifest build would otherwise compile drogon and
  // aws-sdk-cpp from source once per case, which is hours, not a gate.
  const installed = join(repo, "build/vcpkg_installed");
  if (existsSync(installed)) {
    mkdirSync(join(root, "build"), { recursive: true });
    symlinkSync(installed, join(root, "build/vcpkg_installed"), "dir");
  }

  const solutions = join(root, "content/tasks/demo/solutions");
  mkdirSync(solutions, { recursive: true });
  writeFileSync(join(solutions, "main.cpp"), `${source}\n`);

  if (examples) {
    const dir = join(root, "content/tasks/demo/examples");
    mkdirSync(dir, { recursive: true });
    writeFileSync(join(dir, "01.in"), examples.input);
    writeFileSync(join(dir, "01.out"), examples.output);
  }

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
    maxBuffer: 32 * 1024 * 1024,
  });
  return { status: result.status ?? 127, output: `${result.stdout ?? ""}${result.stderr ?? ""}` };
}

/** The version the preset's compiler reports, which a refusal has to name. */
function gnuCompilerVersion(): string {
  const result = spawnSync("g++", ["-dumpfullversion"], { encoding: "utf8" });
  return (result.stdout ?? "").trim();
}

const repo = join(import.meta.dir, "../..");
const work = mkdtempSync(join(tmpdir(), "cms-learn-cpp-falsify-"));

let caught = 0;
let missed = 0;

try {
  for (const testCase of CASES) {
    const root = buildTree(repo, work, testCase.name, testCase.source, testCase.examples);
    // tidy only where the defect is invisible to the sanitizers: it is the slow half, and running it on
    // a case that already fails at build time proves nothing about static analysis.
    const report = runGate(root, { tidy: testCase.staticOnly === true });
    const text = reasonText(report);

    if (!report.ok && text.includes(testCase.expect)) {
      console.log(`caught        ${testCase.name.padEnd(28)} (${testCase.expect})`);
      caught += 1;
    } else if (report.ok) {
      console.log(`MISSED        ${testCase.name.padEnd(28)} — gate passed a planted defect`);
      missed += 1;
    } else {
      console.log(`WRONG REASON  ${testCase.name.padEnd(28)} — expected "${testCase.expect}", got: ${text.slice(0, 160)}`);
      missed += 1;
    }
  }

  // The control: a correct solution must pass, otherwise the gate is failing everything and proves
  // nothing about the defects above.
  const cleanRoot = buildTree(repo, work, "clean-control", CLEAN_SOURCE, { input: "3\n", output: "3\n" });
  const clean = runGate(cleanRoot);
  if (clean.ok) {
    console.log(`behaved       ${"clean-control".padEnd(28)} (a correct solution passes)`);
    caught += 1;
  } else {
    console.log(`WRONG RESULT  ${"clean-control".padEnd(28)} — correct solution rejected: ${reasonText(clean).slice(0, 160)}`);
    missed += 1;
  }

  // An empty tree must not be green: "nothing to compile" is unplugged, not passing.
  const emptyRoot = join(work, "empty-tree");
  mkdirSync(join(emptyRoot, "content/tasks"), { recursive: true });
  const empty = runGate(emptyRoot);
  if (!empty.ok && reasonText(empty).includes("no C++ source")) {
    console.log(`behaved       ${"empty-tree".padEnd(28)} (refuses an empty run)`);
    caught += 1;
  } else {
    console.log(`WRONG RESULT  ${"empty-tree".padEnd(28)} — reported green with no sources`);
    missed += 1;
  }

  // The compiler refusal is a configure-time defect: raise the required major above the installed compiler
  // and configure must stop naming both numbers. A check that never fires leaves the configure green, so
  // this case fails on that instead of passing for the wrong reason.
  const minimumRoot = buildTree(repo, work, "compiler-minimum", CLEAN_SOURCE);
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
  const clangRoot = buildTree(repo, work, "compiler-family", CLEAN_SOURCE);
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
  rmSync(work, { recursive: true, force: true });
}

console.log(`\n${caught} case(s) behaved, ${missed} did not`);
process.exit(missed === 0 ? 0 : 1);
