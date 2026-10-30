#!/usr/bin/env bun
// Renders deploy/docker-compose.yml from release.toml and deploy/stack.template.yml, and verifies that
// the committed file is what those two produce.
//
// The template owns the stack's shape and the reasons written beside each service; release.toml owns
// what actually runs. A hand edit to the rendered file is therefore drift, and this is what sees it.
//
// Usage: bun run tools/render-stack.ts [--check] [--root DIR]
//   --check  compare with the committed file instead of writing it
//   --root   directory holding release.toml and deploy/ (used by the falsification harness)

import { existsSync } from "node:fs";

type Kind = "pinned" | "built" | "unpublished";

interface ImageEntry {
  readonly kind: Kind;
  readonly ref: string;
  readonly digest?: string;
  readonly context?: string;
}

interface Release {
  readonly version: string;
  readonly images: ReadonlyMap<string, ImageEntry>;
}

interface Problem {
  readonly where: string;
  readonly message: string;
}

const DIGEST = /^sha256:[0-9a-f]{64}$/;
const IMAGE_TOKEN = /\{\{image:([a-z0-9][a-z0-9-]*)\}\}/g;
const VERSION_TOKEN = /\{\{version\}\}/g;
const RELEASE_TAG = "${TAG:-dev}";

function fail(problems: readonly Problem[]): never {
  process.stderr.write("stack gate FAILED\n");
  for (const problem of problems) {
    process.stderr.write(`  ${problem.where}: ${problem.message}\n`);
  }
  process.exit(1);
}

function flagValue(args: readonly string[], name: string): string | undefined {
  const index = args.indexOf(name);
  return index >= 0 ? args[index + 1] : undefined;
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function readEntry(key: string, raw: unknown, problems: Problem[]): ImageEntry | undefined {
  if (!isRecord(raw)) {
    problems.push({ where: `images.${key}`, message: "is not a table" });
    return undefined;
  }
  const kind = raw["kind"];
  const ref = raw["ref"];
  if (kind !== "pinned" && kind !== "built" && kind !== "unpublished") {
    problems.push({ where: `images.${key}.kind`, message: "must be pinned, built or unpublished" });
    return undefined;
  }
  if (typeof ref !== "string" || ref.length === 0) {
    problems.push({ where: `images.${key}.ref`, message: "must be a non-empty string" });
    return undefined;
  }
  const digest = raw["digest"];
  const context = raw["context"];

  if (kind === "pinned") {
    if (typeof digest !== "string" || !DIGEST.test(digest)) {
      problems.push({ where: `images.${key}.digest`, message: "a pinned image needs sha256:<64 hex>" });
      return undefined;
    }
  } else if (digest !== undefined) {
    problems.push({ where: `images.${key}.digest`, message: `a ${kind} image cannot carry a digest` });
    return undefined;
  }

  if (kind === "built" && (typeof context !== "string" || context.length === 0)) {
    problems.push({ where: `images.${key}.context`, message: "a built image needs its build context" });
    return undefined;
  }
  if (kind !== "built" && context !== undefined) {
    problems.push({ where: `images.${key}.context`, message: `only a built image has a context` });
    return undefined;
  }

  return {
    kind,
    ref,
    ...(typeof digest === "string" ? { digest } : {}),
    ...(typeof context === "string" ? { context } : {}),
  };
}

function readRelease(text: string, problems: Problem[]): Release {
  const parsed: unknown = Bun.TOML.parse(text);
  if (!isRecord(parsed)) {
    problems.push({ where: "release.toml", message: "is not a table" });
    return { version: "", images: new Map() };
  }
  const version = parsed["version"];
  if (typeof version !== "string" || version.length === 0) {
    problems.push({ where: "version", message: "must be a non-empty string" });
  }
  const images = parsed["images"];
  if (!isRecord(images)) {
    problems.push({ where: "images", message: "must declare at least one image" });
    return { version: typeof version === "string" ? version : "", images: new Map() };
  }

  const entries = new Map<string, ImageEntry>();
  for (const [key, raw] of Object.entries(images)) {
    const entry = readEntry(key, raw, problems);
    if (entry !== undefined) {
      entries.set(key, entry);
    }
  }
  return { version: typeof version === "string" ? version : "", images: entries };
}

function renderImage(entry: ImageEntry): string {
  return entry.kind === "pinned" ? `${entry.ref}@${entry.digest}` : `${entry.ref}:${RELEASE_TAG}`;
}

function checkCoverage(template: string, release: Release, deployDir: string, problems: Problem[]): void {
  const referenced = new Set<string>();
  for (const match of template.matchAll(IMAGE_TOKEN)) {
    const key = match[1];
    if (key === undefined) {
      continue;
    }
    referenced.add(key);
    if (!release.images.has(key)) {
      problems.push({ where: `deploy/stack.template.yml`, message: `names {{image:${key}}}, absent from release.toml` });
    }
  }
  for (const [key, entry] of release.images) {
    if (!referenced.has(key)) {
      problems.push({ where: `images.${key}`, message: "is declared but no service uses it" });
    }
    if (entry.kind === "built" && entry.context !== undefined) {
      const dockerfile = `${deployDir}/${entry.context}/Dockerfile`;
      if (!existsSync(dockerfile)) {
        problems.push({ where: `images.${key}.context`, message: `has no Dockerfile at ${dockerfile}` });
      }
    }
  }
}

function render(template: string, release: Release): string {
  const withImages = template.replace(IMAGE_TOKEN, (_token, key: string) => {
    const entry = release.images.get(key);
    if (entry === undefined) {
      throw new Error(`unresolved image key: ${key}`);
    }
    return renderImage(entry);
  });
  return withImages.replace(VERSION_TOKEN, release.version);
}

function firstDifference(committed: string, produced: string): string {
  const left = committed.split("\n");
  const right = produced.split("\n");
  const limit = Math.max(left.length, right.length);
  for (let index = 0; index < limit; index += 1) {
    if (left[index] !== right[index]) {
      return [
        `line ${index + 1}`,
        `  committed: ${left[index] ?? "<absent>"}`,
        `  produced:  ${right[index] ?? "<absent>"}`,
      ].join("\n");
    }
  }
  return "no line differs; the files end differently";
}

async function main(): Promise<void> {
  const args = process.argv.slice(2);
  const root = flagValue(args, "--root") ?? process.cwd();
  const deployDir = `${root}/deploy`;
  const problems: Problem[] = [];

  const releaseText = await Bun.file(`${root}/release.toml`).text();
  const template = await Bun.file(`${deployDir}/stack.template.yml`).text();
  const release = readRelease(releaseText, problems);
  checkCoverage(template, release, deployDir, problems);
  if (problems.length > 0) {
    fail(problems);
  }

  const produced = render(template, release);
  const counts = { pinned: 0, built: 0, unpublished: 0 };
  for (const entry of release.images.values()) {
    counts[entry.kind] += 1;
  }

  if (!args.includes("--check")) {
    await Bun.write(`${deployDir}/docker-compose.yml`, produced);
    process.stdout.write(`stack: wrote deploy/docker-compose.yml for ${release.version}\n`);
  } else {
    const committed = await Bun.file(`${deployDir}/docker-compose.yml`).text();
    if (committed !== produced) {
      process.stderr.write(`stack gate FAILED: deploy/docker-compose.yml is not what release.toml produces\n`);
      process.stderr.write(`  ${firstDifference(committed, produced)}\n`);
      process.stderr.write(`  regenerate with: bun run tools/render-stack.ts\n`);
      process.exit(1);
    }
  }

  process.stdout.write(
    `  ${release.images.size} image(s): ${counts.pinned} pinned, ${counts.built} built, ${counts.unpublished} unpublished\n`,
  );
  if (counts.unpublished > 0) {
    process.stdout.write(`  ${counts.unpublished} image(s) are not published yet; this release is not deployable\n`);
  }
}

await main();
