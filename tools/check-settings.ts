// The catalogue questions that need the parsed JSON, answered from one parse.
//
// check-settings.sh asks two things of deploy/catalogue/settings.json: that no secret declares a default, and
// that every setting the service reads is declared. Asking them in two processes read and parsed the same
// 108-entry file twice; both answers come from the one parse here.
import { readFileSync } from "node:fs";

interface CatalogueEntry {
  key: string;
  tier: string;
  default: string;
}

interface Catalogue {
  settings: CatalogueEntry[];
}

const SECRET_TIER = "secret";
const DECLARATION = /SettingDefault\{"([^"]+)"/g;

function secretDefaults(entries: CatalogueEntry[]): string[] {
  return entries
    .filter((entry) => entry.tier === SECRET_TIER && entry.default !== "")
    .map((entry) => entry.key);
}

function undeclaredKeys(entries: CatalogueEntry[], source: string): string[] {
  const known = new Set(entries.map((entry) => entry.key));
  const missing = new Set<string>();
  for (const match of source.matchAll(DECLARATION)) {
    const key = match[1] ?? "";
    if (!known.has(key)) {
      missing.add(key);
    }
  }
  return [...missing];
}

function main(): void {
  const [cataloguePath, codeDefaultsPath] = process.argv.slice(2);
  if (cataloguePath === undefined) {
    console.error("  usage: check-settings.ts <catalogue.json> [settings_defaults.hpp]");
    process.exit(2);
  }

  const catalogue = JSON.parse(readFileSync(cataloguePath, "utf8")) as Catalogue;
  const failures: string[] = [];

  const secrets = secretDefaults(catalogue.settings);
  if (secrets.length > 0) {
    failures.push(`a secret setting declares a default: ${secrets.join(", ")}`);
  }

  if (codeDefaultsPath !== undefined) {
    const missing = undeclaredKeys(catalogue.settings, readFileSync(codeDefaultsPath, "utf8"));
    if (missing.length > 0) {
      failures.push(`a setting the code reads is not in the catalogue: ${missing.join(", ")}`);
    }
  }

  if (failures.length > 0) {
    for (const failure of failures) {
      console.error(`  ${failure}`);
    }
    process.exit(1);
  }

  console.log(`  ${catalogue.settings.length} setting(s) satisfy both rules`);
}

main();
