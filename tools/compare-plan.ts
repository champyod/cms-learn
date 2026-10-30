#!/usr/bin/env bun
// Compares two pgschema plan documents while ignoring the timestamp they carry from render time.
//
// A plan is only reviewable if the committed copy still describes what the schema source produces.
// Comparing bytes cannot answer that, because every render stamps created_at. Comparing the
// normalised documents can, and it also reports the exact field that moved when they diverge.
//
// Usage: bun run tools/compare-plan.ts <fresh.json> <committed.json>
//   0 — both describe the same migration
//   1 — they differ, with the first difference named on stderr
//   2 — a file could not be read or parsed

import { existsSync } from "node:fs";

const TIMESTAMP_FIELD = "created_at";

interface Difference {
  readonly path: string;
  readonly fresh: string;
  readonly committed: string;
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function normalise(value: unknown): unknown {
  if (Array.isArray(value)) {
    return value.map(normalise);
  }
  if (isRecord(value)) {
    const sorted: Record<string, unknown> = {};
    for (const key of Object.keys(value).sort()) {
      if (key === TIMESTAMP_FIELD) {
        continue;
      }
      sorted[key] = normalise(value[key]);
    }
    return sorted;
  }
  return value;
}

function describe(value: unknown): string {
  const text = JSON.stringify(value);
  if (text === undefined) {
    return "undefined";
  }
  return text.length <= 160 ? text : `${text.slice(0, 157)}...`;
}

function firstDifference(fresh: unknown, committed: unknown, path: string): Difference | undefined {
  if (Array.isArray(fresh) && Array.isArray(committed)) {
    if (fresh.length !== committed.length) {
      return { path: `${path}.length`, fresh: String(fresh.length), committed: String(committed.length) };
    }
    for (let index = 0; index < fresh.length; index += 1) {
      const nested = firstDifference(fresh[index], committed[index], `${path}[${index}]`);
      if (nested !== undefined) {
        return nested;
      }
    }
    return undefined;
  }
  if (isRecord(fresh) && isRecord(committed)) {
    const keys = [...new Set([...Object.keys(fresh), ...Object.keys(committed)])].sort();
    for (const key of keys) {
      if (key === TIMESTAMP_FIELD) {
        continue;
      }
      if (!(key in fresh) || !(key in committed)) {
        return {
          path: `${path}.${key}`,
          fresh: key in fresh ? describe(fresh[key]) : "<absent>",
          committed: key in committed ? describe(committed[key]) : "<absent>",
        };
      }
      const nested = firstDifference(fresh[key], committed[key], `${path}.${key}`);
      if (nested !== undefined) {
        return nested;
      }
    }
    return undefined;
  }
  if (JSON.stringify(fresh) !== JSON.stringify(committed)) {
    return { path, fresh: describe(fresh), committed: describe(committed) };
  }
  return undefined;
}

async function readPlan(path: string): Promise<unknown> {
  if (!existsSync(path)) {
    throw new Error(`${path} does not exist`);
  }
  return JSON.parse(await Bun.file(path).text());
}

async function main(): Promise<void> {
  const [freshPath, committedPath] = process.argv.slice(2);
  if (freshPath === undefined || committedPath === undefined) {
    process.stderr.write("usage: compare-plan.ts <fresh.json> <committed.json>\n");
    process.exit(2);
  }

  const fresh = normalise(await readPlan(freshPath));
  const committed = normalise(await readPlan(committedPath));
  const difference = firstDifference(fresh, committed, "plan");
  if (difference === undefined) {
    process.stdout.write("  plan documents match\n");
    return;
  }

  process.stderr.write(`plan differs at ${difference.path}\n`);
  process.stderr.write(`  fresh:     ${difference.fresh}\n`);
  process.stderr.write(`  committed: ${difference.committed}\n`);
  process.exit(1);
}

await main().catch((error: unknown) => {
  process.stderr.write(`compare-plan: ${error instanceof Error ? error.message : String(error)}\n`);
  process.exit(2);
});
