import { closeSync, mkdirSync, mkdtempSync, openSync, readFileSync, readdirSync, rmSync, writeFileSync } from "node:fs";
import { homedir } from "node:os";
import { join } from "node:path";

const SCRATCH_ROOT = process.env.CMS_FALSIFY_SCRATCH ?? join(process.env.XDG_CACHE_HOME ?? join(homedir(), ".cache"), "cms-learn", "falsify");
const RUNNER_ROOT = join(SCRATCH_ROOT, "tools");
const SCRATCH_PREFIX = "cms-learn-tools-falsify-";
const LOCK_NAME = "run.lock";

interface LockHolder { pid: number; startedAt: string }

const isRunning = (pid: number): boolean => {
  try { process.kill(pid, 0); return true; } catch (error) { return (error as NodeJS.ErrnoException).code !== "ESRCH"; }
};

const tryCreate = (path: string): boolean => {
  try { closeSync(openSync(path, "wx")); return true; } catch (error) {
    if ((error as NodeJS.ErrnoException).code !== "EEXIST") throw error;
    return false;
  }
};

function acquireLock(root: string): () => void {
  const path = join(root, LOCK_NAME);
  let holder: LockHolder | null = null;
  if (!tryCreate(path)) {
    try {
      const [pid, at] = readFileSync(path, "utf8").trim().split("\n");
      const value = Number.parseInt(pid ?? "", 10);
      if (Number.isInteger(value) && value > 0) holder = { pid: value, startedAt: at ?? "unknown" };
    } catch { holder = null; }
    // Only a dead pid proves that a run has finished; a lock whose holder cannot be read is never cleared.
    if (holder === null || isRunning(holder.pid)) {
      const who = holder === null ? "a lock file that cannot be read" : `pid ${holder.pid} since ${holder.startedAt}`;
      console.error(`scratch root busy: ${path} is held by ${who}`);
      process.exit(1);
    }
    rmSync(path, { force: true });
    if (!tryCreate(path)) throw new Error(`could not retake the lock at ${path} after clearing pid ${holder.pid}`);
  }
  writeFileSync(path, `${process.pid}\n${new Date().toISOString()}\n`);
  return (): void => rmSync(path, { force: true });
}

function sweepOrphans(root: string): void {
  for (const entry of readdirSync(root, { withFileTypes: true })) {
    if (entry.isDirectory() && entry.name.startsWith(SCRATCH_PREFIX)) rmSync(join(root, entry.name), { recursive: true, force: true });
  }
}

/** Claims the scratch root, sweeps the trees a killed run abandoned, and returns the release to call in a finally. */
export function openScratch(): () => void {
  mkdirSync(RUNNER_ROOT, { recursive: true });
  const release = acquireLock(RUNNER_ROOT);
  try {
    sweepOrphans(RUNNER_ROOT);
  } catch (error) {
    release();
    throw error;
  }
  return release;
}

/** Creates a fresh case root inside the scratch root; the caller removes it in a finally. */
export function openCaseRoot(): string {
  return mkdtempSync(join(RUNNER_ROOT, SCRATCH_PREFIX));
}
