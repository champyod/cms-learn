// The catalogue questions that need the parsed JSON and the schema, answered from one parse.
//
// check-settings.sh asks check-settings.ts four things: that no secret declares a default, that every
// setting the service reads is declared in the catalogue, and that every catalogue entry — and every key the
// code reads — can actually be stored under deploy/schema/settings.sql. The last question is E2.2: a setting
// declared in code but absent from the schema has no row the service could ever read, and before this check
// nothing noticed. Asking them in separate processes re-read the same 108-entry file each time.
import { readFileSync } from "node:fs";
import { join } from "node:path";

interface CatalogueEntry {
  key: string;
  kind: string;
  tier: string;
  default: string;
}

interface Catalogue {
  settings: CatalogueEntry[];
}

interface SchemaRules {
  keyPattern: RegExp;
  kinds: Set<string>;
  tiers: Set<string>;
  boolValues: Set<string>;
  intPattern: RegExp;
  intMinimum: bigint;
  intMaximum: bigint;
}

const SECRET_TIER = "secret";
const DECLARATION = /SettingDefault\{"([^"]+)"/g;
const ISSUE = "deploy/schema/settings.sql";

function codeDefaultKeys(source: string): string[] {
  return [...source.matchAll(DECLARATION)].map((match) => match[1] ?? "").filter((key) => key !== "");
}

function secretDefaults(entries: CatalogueEntry[]): string[] {
  return entries
    .filter((entry) => entry.tier === SECRET_TIER && entry.default !== "")
    .map((entry) => entry.key);
}

function undeclaredKeys(entries: CatalogueEntry[], keys: readonly string[]): string[] {
  const known = new Set(entries.map((entry) => entry.key));
  return [...new Set(keys.filter((key) => !known.has(key)))];
}

function quoted(text: string): string[] {
  return [...text.matchAll(/'([^']*)'/g)].map((match) => match[1] ?? "");
}

/// The rules are read from the DDL rather than restated here: a schema change that moves a boundary must
/// change this check with it, and a rule whose shape moved is named instead of silently skipped.
function readSchemaRules(directory: string): { rules: SchemaRules | null; failures: string[] } {
  const failures: string[] = [];
  const read = (name: string): string | null => {
    try {
      return readFileSync(join(directory, name), "utf8");
    } catch {
      failures.push(`cannot read ${join(directory, name)}; ${ISSUE} is where a setting row has to fit`);
      return null;
    }
  };
  const settings = read("settings.sql");
  const types = read("types.sql");
  if (settings === null || types === null) {
    return { rules: null, failures };
  }

  const keyShape = /CONSTRAINT settings_key_shape CHECK \(key ~ '([^']+)'\)/.exec(settings);
  const tierType = /CREATE TYPE setting_tier AS ENUM \(([^)]*)\)/.exec(settings);
  const kindType = /CREATE TYPE setting_kind AS ENUM \(([^)]*)\)/.exec(types);
  const boolRule = /WHEN 'bool' THEN value_text IN \(([^)]*)\)/.exec(settings);
  const intRule = /WHEN 'int' THEN\s+CASE\s+WHEN value_text ~ '([^']+)'/.exec(settings);
  const intRange = /value_text::numeric BETWEEN (-?\d+) AND (-?\d+)/.exec(settings);
  if (
    keyShape === null ||
    tierType === null ||
    kindType === null ||
    boolRule === null ||
    intRule === null ||
    intRange === null
  ) {
    failures.push(`${ISSUE} no longer states the rules this check reads: key shape, kind, tier, or value shape`);
    return { rules: null, failures };
  }

  return {
    rules: {
      keyPattern: new RegExp(keyShape[1] ?? ""),
      kinds: new Set(quoted(kindType[1] ?? "")),
      tiers: new Set(quoted(tierType[1] ?? "")),
      boolValues: new Set(quoted(boolRule[1] ?? "")),
      intPattern: new RegExp(intRule[1] ?? ""),
      intMinimum: BigInt(intRange[1] ?? "0"),
      intMaximum: BigInt(intRange[2] ?? "0"),
    },
    failures,
  };
}

function defaultFits(kind: string, value: string, rules: SchemaRules): boolean {
  if (kind === "bool") {
    return rules.boolValues.has(value);
  }
  if (kind === "int") {
    if (!rules.intPattern.test(value)) {
      return false;
    }
    const number = BigInt(value);
    return number >= rules.intMinimum && number <= rules.intMaximum;
  }
  return true;
}

function cannotStore(entry: CatalogueEntry, rules: SchemaRules): string | null {
  if (!rules.keyPattern.test(entry.key)) {
    return `setting "${entry.key}" cannot be stored: its key does not match settings_key_shape`;
  }
  if (!rules.kinds.has(entry.kind)) {
    return `setting "${entry.key}" cannot be stored: kind "${entry.kind}" is not in setting_kind`;
  }
  if (!rules.tiers.has(entry.tier)) {
    return `setting "${entry.key}" cannot be stored: tier "${entry.tier}" is not in setting_tier`;
  }
  if (!defaultFits(entry.kind, entry.default, rules)) {
    return `setting "${entry.key}" cannot be stored: its default "${entry.default}" is not a ${entry.kind} value`;
  }
  return null;
}

function main(): void {
  const [cataloguePath, codeDefaultsPath, schemaDirectory] = process.argv.slice(2);
  if (cataloguePath === undefined) {
    console.error("  usage: check-settings.ts <catalogue.json> [settings_defaults.hpp] [schema dir]");
    process.exit(2);
  }

  const catalogue = JSON.parse(readFileSync(cataloguePath, "utf8")) as Catalogue;
  const failures: string[] = [];

  const secrets = secretDefaults(catalogue.settings);
  if (secrets.length > 0) {
    failures.push(`a secret setting declares a default: ${secrets.join(", ")}`);
  }

  const { rules, failures: schemaFailures } = readSchemaRules(
    schemaDirectory ?? join(import.meta.dir, "..", "deploy", "schema"),
  );
  failures.push(...schemaFailures);

  if (rules !== null) {
    for (const entry of catalogue.settings) {
      const refusal = cannotStore(entry, rules);
      if (refusal !== null) {
        failures.push(refusal);
      }
    }
  }

  if (codeDefaultsPath !== undefined) {
    const keys = codeDefaultKeys(readFileSync(codeDefaultsPath, "utf8"));
    const missing = undeclaredKeys(catalogue.settings, keys);
    if (missing.length > 0) {
      failures.push(`a setting the code reads is not in the catalogue: ${missing.join(", ")}`);
    }
    if (rules !== null) {
      for (const key of keys) {
        if (!rules.keyPattern.test(key)) {
          failures.push(
            `setting "${key}" the code reads can never have a row: its key does not match settings_key_shape`,
          );
        }
      }
    }
  }

  if (failures.length > 0) {
    for (const failure of failures) {
      console.error(`  ${failure}`);
    }
    process.exit(1);
  }

  console.log(
    `  ${catalogue.settings.length} setting(s) fit the catalogue, the code and ${ISSUE}`,
  );
}

main();
