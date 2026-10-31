// Turns the design package into the templates the engine ships.
//
// Two modes, one component tree: `--check` renders the template mode and compares it with what is committed,
// and also refuses a template with no page behind it. That is the falsifier's hook — a component that was
// renamed or deleted cannot leave a stale template serving the old markup.
import { readFileSync, writeFileSync, readdirSync, existsSync, mkdirSync } from "node:fs";
import { join, isAbsolute } from "node:path";
import { pages, render } from "../design/render.tsx";

const root = join(import.meta.dir, "..");
const defaultDir = join(root, "api", "templates");

const args = process.argv.slice(2);
const check = args.includes("--check");
const dirArg = args.find((argument) => !argument.startsWith("--"));
const target = dirArg === undefined ? defaultDir : isAbsolute(dirArg) ? dirArg : join(root, dirArg);

const failures: string[] = [];

if (!check) {
  mkdirSync(target, { recursive: true });
}

for (const name of Object.keys(pages)) {
  const rendered = render(name, "template");
  const path = join(target, `${name}.html`);
  if (!check) {
    writeFileSync(path, rendered, "utf8");
    continue;
  }
  if (!existsSync(path)) {
    failures.push(`${name}: the page has no template at ${path}`);
    continue;
  }
  const committed = readFileSync(path, "utf8");
  if (committed !== rendered) {
    failures.push(`${name}: the committed template is not what the component produces`);
  }
}

if (check && existsSync(target)) {
  for (const file of readdirSync(target)) {
    if (!file.endsWith(".html")) continue;
    const name = file.slice(0, -".html".length);
    if (!(name in pages)) {
      failures.push(`${file}: a template with no page behind it`);
    }
  }
}

if (!check) {
  console.log(`ported ${Object.keys(pages).length} page(s) to ${target}`);
  process.exit(0);
}
if (failures.length > 0) {
  for (const failure of failures) console.error(`  ${failure}`);
  process.exit(1);
}
console.log(`  ${Object.keys(pages).length} page(s) match their templates`);
