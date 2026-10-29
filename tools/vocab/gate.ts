// Vocabulary gate.
//
// The vocabulary is the one place a task tag resolves, so the failures that matter are silent
// ones: a syllabus statement nobody mapped, a tag that means two things, a hierarchy a chain of
// tags can loop through, a stored topic id that stopped resolving. Each prints as a violation and
// exits non-zero; the counts are printed on both outcomes so a partial vocabulary cannot pass as
// coverage.
import { readFileSync } from "node:fs";
import { join } from "node:path";

const repoRoot = join(import.meta.dir, "..", "..");
const contentDir = join(repoRoot, "content");
const vocabDir = join(contentDir, "vocab");

const violations: string[] = [];
const fail = (message: string): void => {
  violations.push(message);
};

const readJson = <T>(path: string): T => JSON.parse(readFileSync(path, "utf8")) as T;

interface Statement { id: string; group: string; text: string }
interface RawSource { source: string; total: number; groups: { id: string; title: string }[]; statements: Statement[] }
interface Alias { name: string; source: string; section: string }
interface VocabNode {
  id: string;
  en: string;
  th: string | null;
  th_source: { source: string; topic?: string; statement?: string } | null;
  parent: string | null;
  aliases: Alias[];
  sources: string[];
}
interface Mapping {
  version: number;
  nodes: { id: string; en: string; parent: string | null }[];
  mapped: Record<string, string>;
  dropped: Record<string, string>;
}
interface Topic { id: string; en: string; th: string }

const sourceNames = ["ioi", "usaco", "posn", "codeforces"];
const raw: RawSource[] = sourceNames.map((name) => readJson<RawSource>(join(vocabDir, "raw", `${name}.json`)));
const mapping = readJson<Mapping>(join(vocabDir, "mapping.json"));
const vocabulary = readJson<{ nodes: VocabNode[] }>(join(contentDir, "vocab.json"));
const topics = readJson<{ topics: Topic[] }>(join(contentDir, "topics.json")).topics;
const modules = readJson<{ modules: { id: string; topics: string[] }[] }>(join(contentDir, "modules.json"));

const nodes = new Map(vocabulary.nodes.map((node) => [node.id, node]));
const statements = new Map<string, { source: string; text: string; section: string }>();
for (const source of raw) {
  for (const statement of source.statements) statements.set(statement.id, {
    source: source.source,
    text: statement.text,
    section: statement.group,
  });
}

// ── every raw statement is mapped or dropped, exactly once ─────────────────────────────────────
const perSource = new Map<string, { total: number; mapped: number; dropped: number }>();
for (const source of raw) perSource.set(source.source, { total: source.total, mapped: 0, dropped: 0 });

if (raw.some((source) => source.total !== source.statements.length)) {
  for (const source of raw) {
    if (source.total !== source.statements.length) {
      fail(`${source.source}: total ${source.total} but ${source.statements.length} statements`);
    }
  }
}

for (const [id, nodeId] of Object.entries(mapping.mapped)) {
  const statement = statements.get(id);
  if (!statement) {
    fail(`mapping: "${id}" is mapped but no raw source holds it`);
    continue;
  }
  if (id in mapping.dropped) fail(`mapping: "${id}" is both mapped and dropped`);
  if (!nodes.has(nodeId)) fail(`mapping: "${id}" maps to unknown node "${nodeId}"`);
  perSource.get(statement.source)!.mapped += 1;
}

for (const [id, reason] of Object.entries(mapping.dropped)) {
  const statement = statements.get(id);
  if (!statement) {
    fail(`mapping: "${id}" is dropped but no raw source holds it`);
    continue;
  }
  if (reason.trim().length === 0) fail(`mapping: "${id}" is dropped without a reason`);
  perSource.get(statement.source)!.dropped += 1;
}

for (const source of raw) {
  const counts = perSource.get(source.source)!;
  if (counts.mapped + counts.dropped !== counts.total) {
    fail(`${source.source}: ${counts.mapped} mapped + ${counts.dropped} dropped != ${counts.total} statements`);
  }
  const covered = new Set([...Object.keys(mapping.mapped), ...Object.keys(mapping.dropped)]);
  for (const statement of source.statements) {
    if (!covered.has(statement.id)) fail(`${source.source}: "${statement.text}" (${statement.id}) is neither mapped nor dropped`);
    if (statement.text.trim().length === 0) fail(`${source.source}: "${statement.id}" has no text`);
  }
}

// ── nodes ─────────────────────────────────────────────────────────────────────────────────────
let aliasCount = 0;
const aliasOwner = new Map<string, string>();
const seenNode = new Set<string>();
for (const node of vocabulary.nodes) {
  if (seenNode.has(node.id)) fail(`vocab: duplicate node id "${node.id}"`);
  seenNode.add(node.id);
  if (node.en.trim().length === 0) fail(`node ${node.id}: no English name`);

  const own = new Set<string>();
  for (const alias of node.aliases) {
    aliasCount += 1;
    if (own.has(alias.name)) fail(`node ${node.id}: alias "${alias.name}" repeats inside the node`);
    own.add(alias.name);
    const key = alias.name.trim().toLowerCase().replace(/\s+/g, " ");
    const owner = aliasOwner.get(key);
    if (owner && owner !== node.id) fail(`alias "${alias.name}" is on both ${owner} and ${node.id}`);
    aliasOwner.set(key, node.id);

    const statement = [...statements.values()].find(
      (candidate) => candidate.text === alias.name && candidate.source === alias.source && candidate.section === alias.section,
    );
    if (!statement) fail(`node ${node.id}: alias "${alias.name}" is not a statement of ${alias.source}`);
    if (!node.sources.includes(alias.source)) fail(`node ${node.id}: source "${alias.source}" missing from sources[]`);
  }
}

for (const node of mapping.nodes) {
  const built = nodes.get(node.id);
  if (!built) {
    fail(`vocab: node "${node.id}" from the mapping is missing — run tools/vocab/build.ts`);
    continue;
  }
  if (built.en !== node.en) fail(`vocab: node "${node.id}" English name differs from the mapping`);
}

for (const [id, nodeId] of Object.entries(mapping.mapped)) {
  const statement = statements.get(id);
  const node = nodes.get(nodeId);
  if (!statement || !node) continue;
  const present = node.aliases.some(
    (alias) => alias.name === statement.text && alias.source === statement.source && alias.section === statement.section,
  );
  if (!present) fail(`vocab: "${statement.text}" is mapped to ${nodeId} but not listed as its alias — run tools/vocab/build.ts`);
}

// ── hierarchy ─────────────────────────────────────────────────────────────────────────────────
for (const node of vocabulary.nodes) {
  if (node.parent !== null && !nodes.has(node.parent)) fail(`node ${node.id}: parent "${node.parent}" does not exist`);
  if (node.parent === node.id) fail(`node ${node.id}: is its own parent`);
}
const state = new Map<string, "visiting" | "done">();
const walk = (id: string, path: string[]): void => {
  const current = state.get(id);
  if (current === "done") return;
  if (current === "visiting") {
    fail(`vocab: parent cycle ${[...path, id].join(" -> ")}`);
    return;
  }
  state.set(id, "visiting");
  const parent = nodes.get(id)?.parent;
  if (parent) walk(parent, [...path, id]);
  state.set(id, "done");
};
for (const node of vocabulary.nodes) walk(node.id, []);

if (!vocabulary.nodes.some((node) => node.parent !== null)) fail("vocab: no hierarchy at all");

// ── Thai names carry provenance ───────────────────────────────────────────────────────────────
const topicById = new Map(topics.map((topic) => [topic.id, topic]));
for (const node of vocabulary.nodes) {
  if (node.th === null) {
    if (node.th_source !== null) fail(`node ${node.id}: th is null but th_source is set`);
    continue;
  }
  if (node.th_source === null) {
    fail(`node ${node.id}: Thai name "${node.th}" has no provenance`);
    continue;
  }
  if (node.th_source.source === "topics") {
    const topic = topicById.get(node.th_source.topic ?? "");
    if (!topic) fail(`node ${node.id}: th_source topic "${node.th_source.topic}" does not exist`);
    else if (topic.th !== node.th) fail(`node ${node.id}: Thai name is not the topic's Thai name`);
  } else if (node.th_source.source === "posn") {
    const statement = statements.get(node.th_source.statement ?? "");
    if (!statement) fail(`node ${node.id}: th_source statement "${node.th_source.statement}" does not exist`);
    else if (!statement.text.includes(node.th)) fail(`node ${node.id}: Thai name is not wording from the POSN source`);
  } else {
    fail(`node ${node.id}: unknown th_source "${node.th_source.source}"`);
  }
}

// ── stored references still resolve ───────────────────────────────────────────────────────────
let missingTopics = 0;
for (const topic of topics) {
  if (!nodes.has(topic.id)) {
    missingTopics += 1;
    fail(`topics.json: id "${topic.id}" no longer resolves in the vocabulary`);
  }
}
for (const module of modules.modules) {
  for (const topic of module.topics ?? []) {
    if (!nodes.has(topic)) fail(`module ${module.id}: topic "${topic}" does not resolve`);
  }
}

// ── verdict ──────────────────────────────────────────────────────────────────────────────────
const counts = sourceNames
  .map((name) => {
    const c = perSource.get(name)!;
    return `${name} ${c.mapped}/${c.dropped}/${c.total}`;
  })
  .join(" · ");
const scanned = `nodes ${vocabulary.nodes.length} · aliases ${aliasCount} · topic ids kept ${topics.length - missingTopics}/${topics.length} · ${counts}`;

if (violations.length > 0) {
  console.error(`vocab gate — ${violations.length} violation(s)`);
  console.error(`scanned: ${scanned}`);
  for (const violation of violations) console.error(`  ✗ ${violation}`);
  process.exit(1);
}

if (vocabulary.nodes.length === 0 || aliasCount === 0) {
  console.error(`vocab gate — nothing to validate (scanned: ${scanned})`);
  process.exit(1);
}

const untranslated = vocabulary.nodes.filter((node) => node.th === null).length;
console.log(`vocab gate — OK · scanned: ${scanned} · untranslated ${untranslated}/${vocabulary.nodes.length}`);
