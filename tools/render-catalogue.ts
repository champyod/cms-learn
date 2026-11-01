// Renders the settings catalogue from docs/settings.md.
//
// The document is the catalogue a person reads and reviews; this turns it into the data the form, the
// service and `cmsctl settings plan` all consume, so there is one source and no second list to keep in step.
// A row that does not parse is a failure, never a skipped line: a settings table with a hole in it would
// silently remove a setting from the admin UI.
import { readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";

const root = join(import.meta.dir, "..");
const source = join(root, "docs", "settings.md");
const target = join(root, "deploy", "catalogue", "settings.json");

interface Entry {
  key: string;
  kind: string;
  tier: string;
  default: string;
  reload: string;
}

const lines = readFileSync(source, "utf8").split("\n");
const entries: Entry[] = [];
const failures: string[] = [];

// A catalogue row is `| \`key\` | kind | tier | \`default\` | reload |`. Section headings and the tier and
// reload tables use the same pipe shape, so a row is only a setting when its first cell is a dotted key.
for (const [index, line] of lines.entries()) {
  if (!line.startsWith("| `")) continue;
  const cells = line
    .split("|")
    .slice(1, -1)
    .map((cell) => cell.trim());
  if (cells.length !== 5) continue;
  if (!/^`[a-z][a-z0-9_]*(\.[a-z][a-z0-9_]*)+`$/.test(cells[0])) continue;

  const key = cells[0].slice(1, -1);
  const kind = cells[1];
  const tier = cells[2];
  // The document writes `empty` for "no default", which is a word rather than a value: a catalogue entry with
  // the literal string "empty" would be a setting whose default is the word.
  const written = cells[3].replace(/`/g, "");
  const fallback = written === "empty" ? "" : written;
  const reload = cells[4];

  if (!["bool", "int", "text"].includes(kind)) {
    failures.push(`line ${index + 1}: ${key} has kind "${kind}"`);
    continue;
  }
  if (!["public", "operator", "secret"].includes(tier)) {
    failures.push(`line ${index + 1}: ${key} has tier "${tier}"`);
    continue;
  }
  entries.push({ key, kind, tier, default: fallback, reload });
}

const seen = new Set<string>();
for (const entry of entries) {
  if (seen.has(entry.key)) {
    failures.push(`${entry.key} is declared twice`);
  }
  seen.add(entry.key);
}

if (failures.length > 0) {
  for (const failure of failures) console.error(`  ${failure}`);
  process.exit(1);
}

const catalogue = {
  version: 1,
  note: "Generated from docs/settings.md by tools/render-catalogue.ts. Edit the document, then run: bun run render:catalogue",
  settings: entries.map((entry) => ({
    key: entry.key,
    kind: entry.kind,
    tier: entry.tier,
    default: entry.default,
    reload: entry.reload,
  })),
};

const rendered = `${JSON.stringify(catalogue, null, 2)}\n`;
if (process.argv.includes("--check")) {
  const committed = readFileSync(target, "utf8");
  if (committed !== rendered) {
    console.error("  deploy/catalogue/settings.json is not what docs/settings.md declares");
    process.exit(1);
  }
  console.log(`  ${entries.length} setting(s) match the document`);
  process.exit(0);
}

writeFileSync(target, rendered, "utf8");
console.log(`rendered ${entries.length} setting(s) to deploy/catalogue/settings.json`);
