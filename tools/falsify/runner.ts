import { spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import { cpSync, existsSync, mkdirSync, readFileSync, readdirSync, readlinkSync, realpathSync, rmSync, symlinkSync } from "node:fs";
import { dirname, join, relative } from "node:path";

import type { FalsifyCase, Gate, GateCommand } from "./contract.ts";
import { gateById, GATES } from "./cases.ts";
import { applyPlant } from "./plants.ts";
import { summaryLine, verdictFor, type CommandResult, type Outcome } from "./report.ts";
import { openCaseRoot, openScratch } from "./scratch.ts";
import { reason } from "./text.ts";

const REPO = realpathSync(join(import.meta.dir, "../.."));
const COMMAND_NOT_FOUND = 127;
const GATE_TIMEOUT_MS = 10 * 60_000;

/** What one gate run produced: every verdict it computed, plus the run-level failure that stopped it, if any. */
export interface GateEvaluation {
  outcomes: readonly Outcome[];
  failure: string | null;
}

const substitute = (text: string, root: string): string => text.replaceAll("{root}", root).replaceAll("{repo}", REPO);

function hashTree(root: string): string {
  const lines: string[] = [];
  const walk = (dir: string): void => {
    for (const entry of readdirSync(dir, { withFileTypes: true })) {
      const path = join(dir, entry.name);
      if (entry.isDirectory()) {
        lines.push(`${relative(root, path)}:dir`);
        walk(path);
      } else if (entry.isSymbolicLink()) lines.push(`${relative(root, path)}:link:${readlinkSync(path)}`);
      else lines.push(`${relative(root, path)}:${createHash("sha256").update(readFileSync(path)).digest("hex")}`);
    }
  };
  walk(root);
  return createHash("sha256").update(lines.sort().join("\n")).digest("hex");
}

const stagedSource = (entry: string): string => {
  const source = join(REPO, entry);
  if (!existsSync(source)) throw new Error(`staged entry ${entry} is not in the repository`);
  return source;
};

function stageTree(gate: Gate, root: string): void {
  for (const entry of gate.stage) cpSync(stagedSource(entry), join(root, entry), { recursive: true });
  for (const entry of gate.link ?? []) {
    mkdirSync(dirname(join(root, entry)), { recursive: true });
    symlinkSync(stagedSource(entry), join(root, entry), "dir");
  }
}

function caseEnv(command: GateCommand, root: string): NodeJS.ProcessEnv {
  const env: NodeJS.ProcessEnv = { ...process.env };
  for (const [key, value] of Object.entries(command.env ?? {})) env[key] = substitute(value, root);
  return env;
}

function runCommand(command: GateCommand, root: string, env: NodeJS.ProcessEnv): CommandResult {
  const args = command.args.map((arg) => substitute(arg, root));
  const cwd = command.cwd === undefined ? root : substitute(command.cwd, root);
  const result = spawnSync(command.command, args, { cwd, env, encoding: "utf8", maxBuffer: 32 * 1024 * 1024, timeout: GATE_TIMEOUT_MS });
  if (result.error !== undefined) {
    if ((result.error as NodeJS.ErrnoException).code === "ETIMEDOUT") {
      throw new Error(`the gate command timed out after ${GATE_TIMEOUT_MS} ms: ${command.command} ${args.join(" ")}`);
    }
    throw new Error(`cannot run ${command.command}: ${result.error.message}`);
  }
  return { status: result.status ?? COMMAND_NOT_FOUND, output: `${result.stdout ?? ""}${result.stderr ?? ""}` };
}

function judgeCase(testCase: FalsifyCase, gate: Gate, root: string): Outcome {
  try {
    stageTree(gate, root);
    const command = testCase.command ?? gate.command;
    const env = caseEnv(command, root);
    const before = hashTree(root);
    const plants = testCase.plant ?? [];
    for (const plant of plants) applyPlant(plant, root, env);
    if (plants.length > 0 && hashTree(root) === before) {
      return { line: `SETUP FAILED  ${testCase.name} — the plant changed nothing in the staged tree (stale pattern?)`, ok: false };
    }
    return verdictFor(testCase, runCommand(command, root, env));
  } catch (error) {
    return { line: `SETUP FAILED  ${testCase.name} — ${reason(error)}`, ok: false };
  }
}

function runCase(testCase: FalsifyCase, gate: Gate): Outcome {
  let root: string;
  try {
    root = openCaseRoot();
  } catch (error) {
    return { line: `SETUP FAILED  ${testCase.name} — cannot create a case tree: ${reason(error)}`, ok: false };
  }
  const state: { outcome: Outcome | null; teardown: string | null } = { outcome: null, teardown: null };
  try {
    state.outcome = judgeCase(testCase, gate, root);
  } finally {
    try {
      rmSync(root, { recursive: true, force: true });
    } catch (error) {
      state.teardown = reason(error);
    }
  }
  if (state.teardown !== null) {
    return { line: `SETUP FAILED  ${testCase.name} — teardown failed: ${state.teardown}`, ok: false };
  }
  if (state.outcome === null) {
    return { line: `SETUP FAILED  ${testCase.name} — the case produced no verdict`, ok: false };
  }
  return state.outcome;
}

/** Runs every case in its own staged tree, collecting verdicts; a run-level failure is returned, never thrown. */
export function evaluateGate(gate: Gate): GateEvaluation {
  const outcomes: Outcome[] = [];
  let failure: string | null = null;
  let release: (() => void) | null = null;
  try {
    release = openScratch();
    for (const testCase of gate.cases) outcomes.push(runCase(testCase, gate));
  } catch (error) {
    failure = reason(error);
  } finally {
    if (release !== null) {
      try {
        release();
      } catch (error) {
        failure = failure === null ? reason(error) : `${failure}; ${reason(error)}`;
      }
    }
  }
  return { outcomes, failure };
}

const printCases = (gate: Gate): void => {
  for (const item of gate.cases) console.log(`${gate.id}\t${item.name}`);
};

function gateByIdOrExit(id: string): Gate | null {
  try { return gateById(id); } catch (error) { console.error(reason(error)); return null; }
}

function runGate(gate: Gate): number {
  const evaluation = evaluateGate(gate);
  evaluation.outcomes.forEach((outcome) => console.log(outcome.line));
  if (evaluation.failure !== null) console.error(`falsification run failed: ${evaluation.failure}`);
  const behaved = evaluation.outcomes.filter((outcome) => outcome.ok).length;
  console.log(summaryLine(behaved, evaluation.outcomes.length - behaved));
  return evaluation.failure === null && behaved === evaluation.outcomes.length ? 0 : 1;
}

function main(argv: readonly string[]): number {
  const usage = "usage: bun run tools/falsify/runner.ts <gate-id> | --list [gate-id]";
  const isList = argv[0] === "--list";
  const ids = isList ? argv.slice(1) : argv;
  if (ids.length > 1 || (!isList && ids.length !== 1)) {
    console.error(usage);
    return 2;
  }
  if (isList && ids.length === 0) {
    GATES.forEach(printCases);
    return 0;
  }
  const gate = gateByIdOrExit(ids[0]);
  if (gate === null) return 2;
  if (isList) {
    printCases(gate);
    return 0;
  }
  return runGate(gate);
}

if (import.meta.main) process.exitCode = main(process.argv.slice(2));
