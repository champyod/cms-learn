#!/usr/bin/env bun
// The feature registry is a promise list: every row names the artifact that proves the feature and, where a
// decision shaped it, the ADR that decided it. A citation that does not resolve tells a reader the platform is
// protected by something it is not, so this gate reads FEATURES.md the way a reader does and refuses by name.
//
// What it checks:
//   1. every backticked path resolves (a glob must match at least one file or directory)
//   2. every ADR reference — `ADR 0046`, `ADR 0035, 0037`, or a `docs/decisions/NNNN...` citation — has a
//      written decision
//   3. the counts line says what the rows say, and a shipped or partial row cites evidence
//
// Usage: bun run tools/check-registry.ts [--root DIR] [--features FILE]
//   --root      directory the cited paths are resolved against (default: the working directory; the falsify
//               harness points it at the checkout while the file under test is a staged copy)
//   --features  the registry file to read (default: <root>/FEATURES.md)

import { existsSync, readdirSync, readFileSync } from "node:fs";
import { join } from "node:path";

type Citation = { readonly line: number; readonly text: string };

const FILE_SUFFIXES = [
  ".c",
  ".cmake",
  ".cpp",
  ".css",
  ".h",
  ".hpp",
  ".html",
  ".json",
  ".js",
  ".lock",
  ".md",
  ".py",
  ".rs",
  ".sh",
  ".sql",
  ".svg",
  ".toml",
  ".ts",
  ".tsx",
  ".txt",
  ".xml",
  ".yaml",
  ".yml",
];

const ADR_NUMBER = /\bADR\s+(\d{4})((?:\s*,\s*\d{4})*)/g;
const ADR_PREFIX = /^docs\/decisions\/(\d{4})$/;
const ADR_PATH = /^docs\/decisions\/(\d{4})-[^/]+$/;
const COUNTS_LINE =
  /Counts at last update:\s*\*\*(\d+) shipped · (\d+) partial · (\d+) planned\*\*\s*\((\d+) features?\)/;
const TICK = /`([^`]+)`/g;
const GLOB = /[*?[{]/;

function flagValue(args: readonly string[], name: string): string | undefined {
  const index = args.indexOf(name);
  return index >= 0 ? args[index + 1] : undefined;
}

// A citation is a path when it has a slash or a file suffix. A span with a space is prose or a command line,
// `/metrics` is a route, and a bare word such as `content` is the subject rather than a location, so none of
// those is resolved against the tree.
function isPathCitation(text: string): boolean {
  if (text.length === 0 || /\s/.test(text) || text.startsWith("/") || text.includes("://")) {
    return false;
  }
  return text.includes("/") || FILE_SUFFIXES.some((suffix) => text.endsWith(suffix));
}

function resolves(root: string, text: string): boolean {
  const target = text.replace(/\/+$/, "");
  if (!GLOB.test(target)) {
    return existsSync(join(root, target));
  }
  return [...new Bun.Glob(target).scanSync({ cwd: root, onlyFiles: false })].length > 0;
}

function decisions(directory: string): string[] {
  try {
    return readdirSync(directory).filter((entry) => entry.endsWith(".md"));
  } catch {
    return [];
  }
}

function counted(markers: readonly string[]): { shipped: number; partial: number; planned: number } {
  return {
    shipped: markers.filter((marker) => marker === "●").length,
    partial: markers.filter((marker) => marker === "◐").length,
    planned: markers.filter((marker) => marker === "○").length,
  };
}

function main(): void {
  const args = process.argv.slice(2);
  const root = flagValue(args, "--root") ?? process.cwd();
  const features = flagValue(args, "--features") ?? join(root, "FEATURES.md");
  const lines = readFileSync(features, "utf8").split("\n");
  const written = decisions(join(root, "docs/decisions"));

  const paths: Citation[] = [];
  const adrs: Citation[] = [];
  const violations: string[] = [];
  const rows: string[][] = [];
  let countsAt = 0;
  let declared: { shipped: number; partial: number; planned: number; total: number } | undefined;

  for (const [index, line] of lines.entries()) {
    const at = index + 1;
    for (const match of line.matchAll(TICK)) {
      const text = match[1] ?? "";
      if (text === "") {
        continue;
      }
      // `docs/decisions/0026` names a decision without its slug, so it is an ADR citation, not a missing file.
      const adrPrefix = ADR_PREFIX.exec(text);
      if (adrPrefix !== null) {
        adrs.push({ line: at, text: `ADR ${adrPrefix[1]}` });
        continue;
      }
      if (!isPathCitation(text)) {
        continue;
      }
      paths.push({ line: at, text });
      const adrPath = ADR_PATH.exec(text);
      if (adrPath !== null) {
        adrs.push({ line: at, text: `ADR ${adrPath[1]}` });
      }
    }
    for (const match of line.matchAll(ADR_NUMBER)) {
      for (const reference of [match[1] ?? "", ...(match[2] ?? "").split(",")]) {
        const number = reference.trim();
        if (number.length > 0) {
          adrs.push({ line: at, text: `ADR ${number}` });
        }
      }
    }
    if (line.startsWith("| F-")) {
      const cells = line.trim().replace(/^\||\|$/g, "").split("|").map((cell) => cell.trim());
      if (cells.length < 5) {
        violations.push(`FEATURES.md:${at}: a registry row has ${cells.length} column(s), expected 5`);
        continue;
      }
      rows.push(cells);
      const state = cells[3] ?? "";
      if (!["●", "◐", "○"].includes(state)) {
        violations.push(`FEATURES.md:${at}: ${cells[0]} has state "${state}", not ●, ◐ or ○`);
      } else if (state !== "○" && (cells[4] ?? "") === "—") {
        violations.push(`FEATURES.md:${at}: ${cells[0]} is ${state === "●" ? "shipped" : "partial"} and cites no evidence`);
      }
    }
    const counts = COUNTS_LINE.exec(line);
    if (counts !== null) {
      countsAt = at;
      declared = {
        shipped: Number(counts[1]),
        partial: Number(counts[2]),
        planned: Number(counts[3]),
        total: Number(counts[4]),
      };
    }
  }

  for (const citation of paths) {
    if (!resolves(root, citation.text)) {
      violations.push(`FEATURES.md:${citation.line}: the cited path \`${citation.text}\` does not exist`);
    }
  }
  for (const citation of adrs) {
    const number = citation.text.split(" ")[1] ?? "";
    if (!written.some((file) => file.startsWith(`${number}-`))) {
      violations.push(`FEATURES.md:${citation.line}: ${citation.text} has no decision under docs/decisions`);
    }
  }
  if (declared === undefined) {
    violations.push("FEATURES.md: no 'Counts at last update' line names the registry's totals");
  } else {
    const markers = counted(rows.map((cells) => cells[3] ?? ""));
    if (markers.shipped !== declared.shipped || markers.partial !== declared.partial ||
        markers.planned !== declared.planned || rows.length !== declared.total) {
      violations.push(
        `FEATURES.md:${countsAt}: the counts line says ${declared.shipped} shipped, ${declared.partial} partial, ` +
          `${declared.planned} planned (${declared.total} features), and the rows say ${markers.shipped} shipped, ` +
          `${markers.partial} partial, ${markers.planned} planned (${rows.length} features)`,
      );
    }
  }

  if (violations.length > 0) {
    console.error("registry gate FAILED");
    for (const violation of violations) {
      console.error(`  ${violation}`);
    }
    process.exit(1);
  }
  const uniquePaths = new Set(paths.map((citation) => citation.text));
  const uniqueAdrs = new Set(adrs.map((citation) => citation.text));
  const markers = counted(rows.map((cells) => cells[3] ?? ""));
  console.log(
    `  ${uniquePaths.size} cited path(s) and ${uniqueAdrs.size} ADR reference(s) resolve; ` +
      `${markers.shipped} shipped, ${markers.partial} partial, ${markers.planned} planned`,
  );
}

main();
