import { createHash } from "node:crypto";
import { existsSync, mkdirSync, readFileSync, readdirSync, writeFileSync } from "node:fs";
import { isAbsolute, join } from "node:path";

// Builds the editor island asset and the licence text that must sit beside it.
//
// The bundle is generated, so the source in web/island/ owns it and this script is the only thing that
// writes it. `--check` builds the same bytes in memory and compares them with the committed asset, which
// is how a source edit that was never rebuilt is caught instead of shipped.
const root = join(import.meta.dir, "..");
const ENTRY = join(root, "web", "island", "editor.ts");
const DEFAULT_OUT = join(root, "web", "public");
const MODULES_DIR = join(root, "node_modules");
const MODULE_MARKER = "node_modules/";
const BUNDLE_NAME = "island.js";
const LICENCE_NAME = "island.LICENSE.txt";
const LICENCE_FILE = /^licen[cs]e/i;
const RULE = "-".repeat(80);

interface Asset {
  bundle: string;
  licence: string;
}

interface LicenceEntry {
  name: string;
  version: string;
  licence: string;
  text: string;
}

const sha256 = (text: string): string => createHash("sha256").update(text).digest("hex");

function packageName(path: string): string | null {
  const marker = path.lastIndexOf(MODULE_MARKER);
  if (marker === -1) {
    return null;
  }
  const segments = path.slice(marker + MODULE_MARKER.length).split("/");
  const first = segments[0];
  if (first === undefined) {
    return null;
  }
  if (!first.startsWith("@")) {
    return first;
  }
  const second = segments[1];
  return second === undefined ? null : `${first}/${second}`;
}

function readLicence(name: string): LicenceEntry {
  const directory = join(MODULES_DIR, name);
  const manifestPath = join(directory, "package.json");
  if (!existsSync(manifestPath)) {
    throw new Error(`the bundle contains ${name}, which is not installed under node_modules`);
  }
  const manifest: { version?: string; license?: string } = JSON.parse(readFileSync(manifestPath, "utf8"));
  const file = readdirSync(directory)
    .filter((candidate) => LICENCE_FILE.test(candidate))
    .sort()[0];
  if (file === undefined) {
    throw new Error(`the bundle contains ${name}, whose licence text is not installed beside it`);
  }
  return {
    name,
    version: manifest.version ?? "unknown",
    licence: manifest.license ?? "unknown",
    text: readFileSync(join(directory, file), "utf8").trim(),
  };
}

function renderLicence(inputs: string[]): string {
  const names = [...new Set(inputs.map(packageName))].filter((name): name is string => name !== null).sort();
  const entries = names.map(readLicence);
  const header = [
    `${BUNDLE_NAME} — built by tools/build-island.ts from web/island/editor.ts.`,
    "The bundle is generated: edit the source, then run that script.",
    "",
    "Bundled packages, each licence reproduced below:",
    ...entries.map((entry) => `  ${entry.name} ${entry.version} — ${entry.licence}`),
    "",
  ].join("\n");
  const bodies = entries.map(
    (entry) => `${RULE}\n${entry.name} ${entry.version}\n\n${entry.text}\n`,
  );
  return `${header}\n${bodies.join("\n")}`;
}

async function build(): Promise<Asset> {
  const result = await Bun.build({
    entrypoints: [ENTRY],
    target: "browser",
    format: "iife",
    minify: true,
    sourcemap: "none",
    metafile: true,
  });
  if (!result.success) {
    for (const log of result.logs) {
      console.error(log);
    }
    throw new Error("the island source did not build");
  }
  if (result.outputs.length !== 1) {
    throw new Error(`the build produced ${result.outputs.length} outputs, expected one`);
  }
  const inputs = Object.keys(result.metafile?.inputs ?? {});
  return {
    bundle: await result.outputs[0].text(),
    licence: renderLicence(inputs),
  };
}

function fail(message: string): never {
  console.error(`island build FAILED: ${message}`);
  process.exit(1);
}

const args = process.argv.slice(2);
const isCheck = args.includes("--check");
const outArg = args.find((argument) => !argument.startsWith("--"));
const out = outArg === undefined ? DEFAULT_OUT : isAbsolute(outArg) ? outArg : join(root, outArg);

const built = await build();
const checksum = sha256(built.bundle);
const bundlePath = join(out, BUNDLE_NAME);
const licencePath = join(out, LICENCE_NAME);

if (!isCheck) {
  mkdirSync(out, { recursive: true });
  writeFileSync(bundlePath, built.bundle);
  writeFileSync(licencePath, built.licence);
  console.log(`island bundle: ${checksum}  ${BUNDLE_NAME}`);
  process.exit(0);
}

const missing = [bundlePath, licencePath].filter((path) => !existsSync(path));
if (missing.length > 0) {
  fail(`the committed asset is missing: ${missing.join(", ")}`);
}

const failures: string[] = [];
const committedBundle = readFileSync(bundlePath, "utf8");
if (committedBundle !== built.bundle) {
  failures.push(
    `${BUNDLE_NAME} is stale: committed sha256 ${sha256(committedBundle)}, the source builds ${checksum}`,
  );
}
if (readFileSync(licencePath, "utf8") !== built.licence) {
  failures.push(`${LICENCE_NAME} is stale: it is not the licence text the source builds`);
}
if (failures.length > 0) {
  for (const failure of failures) {
    console.error(`  ${failure}`);
  }
  fail("the committed island asset is not what web/island/ builds");
}
console.log(`island bundle: OK — ${checksum}`);
