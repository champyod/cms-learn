// Content gate.
//
// The bank is data, so the only thing that can go wrong silently is a broken reference: a module
// whose prerequisite was renamed, a task tagged with a topic the vocabulary dropped, a task marked
// published while its verification never ran. Each of those renders a page that looks correct and
// teaches something wrong, so this validates structure strictly and prints what it scanned — an
// empty or partial bank must not pass as coverage.
import Ajv, { type ErrorObject } from "ajv";
import { existsSync, readFileSync, readdirSync, statSync } from "node:fs";
import { dirname, join, relative } from "node:path";

const repoRoot = join(import.meta.dir, "..");
const contentDir = join(repoRoot, "content");

const violations: string[] = [];
const rel = (p: string) => relative(repoRoot, p);

function readJson<T>(path: string): T | null {
  try {
    return JSON.parse(readFileSync(path, "utf8")) as T;
  } catch (error) {
    violations.push(`${rel(path)}: unreadable JSON (${(error as Error).message})`);
    return null;
  }
}

function schemaErrors(path: string, errors: ErrorObject[] | null | undefined): void {
  for (const error of errors ?? []) {
    const where = error.instancePath.length > 0 ? error.instancePath : "(root)";
    violations.push(`${rel(path)}${where}: ${error.message ?? "invalid"}${error.params ? ` ${JSON.stringify(error.params)}` : ""}`);
  }
}

const ajv = new Ajv({ allErrors: true, strict: true });

// ── topic vocabulary ────────────────────────────────────────────────────────────────────────────
interface TopicEntry { id: string; en: string; th: string }
const topicsFile = readJson<{ version: number; topics: TopicEntry[] }>(join(contentDir, "topics.json"));
const vocabulary = new Set<string>();
if (topicsFile) {
  const seen = new Set<string>();
  for (const topic of topicsFile.topics) {
    if (seen.has(topic.id)) violations.push(`content/topics.json: duplicate topic id "${topic.id}"`);
    seen.add(topic.id);
    vocabulary.add(topic.id);
  }
 }

// ── curriculum ──────────────────────────────────────────────────────────────────────────────────
interface Module { id: string; title: { en: string; th: string }; topics: string[]; prereqs: string[] }
interface Category { id: string; title: { en: string; th: string }; modules: Module[] }
interface Section { id: string; order: number; title: { en: string; th: string }; categories: Category[] }

const curriculumPath = join(contentDir, "curriculum.tree.json");
const curriculum = readJson<{ version: number; sections: Section[] }>(curriculumPath);
const sectionIds = new Set<string>();
const moduleIds = new Set<string>();
const modulePrereqs = new Map<string, string[]>();
let categoryCount = 0;
let moduleCount = 0;

if (curriculum) {
  const validate = ajv.compile(readJson<object>(join(contentDir, "schema/curriculum.schema.json")) ?? {});
  if (!validate(curriculum)) schemaErrors(curriculumPath, validate.errors);

  const ids = new Set<string>();
  const orders = new Set<number>();

  for (const section of curriculum.sections ?? []) {
    if (ids.has(section.id)) violations.push(`curriculum: duplicate id "${section.id}"`);
    ids.add(section.id);
    sectionIds.add(section.id);
    if (orders.has(section.order)) violations.push(`curriculum: duplicate section order ${section.order}`);
    orders.add(section.order);

    for (const category of section.categories ?? []) {
      categoryCount += 1;
      if (ids.has(category.id)) violations.push(`curriculum: duplicate id "${category.id}"`);
      ids.add(category.id);

      for (const module of category.modules ?? []) {
        moduleCount += 1;
        if (ids.has(module.id)) violations.push(`curriculum: duplicate id "${module.id}"`);
        ids.add(module.id);
        moduleIds.add(module.id);
        modulePrereqs.set(module.id, module.prereqs ?? []);
        for (const topic of module.topics ?? []) {
          if (!vocabulary.has(topic)) violations.push(`module ${module.id}: unknown topic "${topic}"`);
        }
      }
    }
  }

  // prerequisites: every target exists, and the graph is acyclic (a cycle is an unlearnable path)
  for (const [moduleId, prereqs] of modulePrereqs) {
    for (const prereq of prereqs) {
      if (!moduleIds.has(prereq)) violations.push(`module ${moduleId}: prerequisite "${prereq}" does not exist`);
    }
  }
  const state = new Map<string, "visiting" | "done">();
  const walk = (moduleId: string, path: string[]): void => {
    const current = state.get(moduleId);
    if (current === "done") return;
    if (current === "visiting") {
      violations.push(`curriculum: prerequisite cycle ${[...path, moduleId].join(" -> ")}`);
      return;
    }
    state.set(moduleId, "visiting");
    for (const prereq of modulePrereqs.get(moduleId) ?? []) walk(prereq, [...path, moduleId]);
    state.set(moduleId, "done");
  };
  for (const moduleId of moduleIds) walk(moduleId, []);
}

// ── tasks ──────────────────────────────────────────────────────────────────────────────────────
interface Task {
  id: string;
  version: number;
  status: "draft" | "verified" | "published" | "rejected";
  level: string;
  topics: string[];
  locales: string[];
  checker: { kind: string; path?: string };
  testdata?: { manifest: string };
  verification: { status: string; reference_solution: string; methods: string[] };
  origin: { kind: string };
}

const tasksDir = join(contentDir, "tasks");
const taskCounts: Record<string, number> = { draft: 0, verified: 0, published: 0, rejected: 0 };
let taskCount = 0;

if (existsSync(tasksDir)) {
  const taskSchema = readJson<object>(join(contentDir, "schema/task.schema.json")) ?? {};
  const validateTask = ajv.compile(taskSchema);

  for (const entry of readdirSync(tasksDir).sort()) {
    const dir = join(tasksDir, entry);
    if (!statSync(dir).isDirectory()) continue;
    const taskPath = join(dir, "task.json");
    if (!existsSync(taskPath)) {
      violations.push(`${rel(dir)}: no task.json`);
      continue;
    }
    const task = readJson<Task>(taskPath);
    if (!task) continue;
    taskCount += 1;

    if (!validateTask(task)) schemaErrors(taskPath, validateTask.errors);

    if (!sectionIds.has(task.level)) violations.push(`${rel(taskPath)}: level "${task.level}" is not a section`);
    for (const topic of task.topics ?? []) {
      if (!vocabulary.has(topic)) violations.push(`${rel(taskPath)}: unknown topic "${topic}"`);
    }
    if (entry !== task.id) violations.push(`${rel(taskPath)}: id "${task.id}" does not match its directory "${entry}"`);

    for (const locale of task.locales ?? []) {
      const statement = join(dir, `statement.${locale}.md`);
      if (!existsSync(statement)) violations.push(`${rel(taskPath)}: missing ${rel(statement)}`);
    }

    const solution = join(dir, task.verification?.reference_solution ?? "");
    if (!existsSync(solution)) violations.push(`${rel(taskPath)}: reference solution ${rel(solution)} missing`);

    if (task.checker?.kind === "testlib" && !task.checker.path) {
      violations.push(`${rel(taskPath)}: testlib checker needs an explicit path`);
    }
    if (task.checker?.path && !existsSync(join(dir, task.checker.path))) {
      violations.push(`${rel(taskPath)}: checker ${task.checker.path} missing`);
    }

    // A published task is what a student can be served. That is the one status that must carry
    // machine evidence, because a task with a wrong expected answer looks exactly like a good one.
    if (task.status === "published" || task.status === "verified") {
      if (task.verification?.status !== "verified") {
        violations.push(`${rel(taskPath)}: status "${task.status}" with verification "${task.verification?.status}"`);
      }
      const required = ["reference-pass", "bruteforce-differential", "limits-measured"];
      for (const method of required) {
        if (!(task.verification?.methods ?? []).includes(method)) {
          violations.push(`${rel(taskPath)}: verification method "${method}" not recorded`);
        }
      }
      if (!task.testdata?.manifest) violations.push(`${rel(taskPath)}: no test data manifest`);
      else if (!existsSync(join(dir, task.testdata.manifest))) {
        violations.push(`${rel(taskPath)}: test data manifest ${task.testdata.manifest} missing`);
      }
    }

    taskCounts[task.status] = (taskCounts[task.status] ?? 0) + 1;
  }
}

// ── verdict ───────────────────────────────────────────────────────────────────────────────────
const scanned =
  `topics ${vocabulary.size} · sections ${sectionIds.size} · categories ${categoryCount} · ` +
  `modules ${moduleCount} · tasks ${taskCount} (draft ${taskCounts.draft}, verified ${taskCounts.verified}, ` +
  `published ${taskCounts.published}, rejected ${taskCounts.rejected})`;

if (violations.length > 0) {
  console.error(`content gate — ${violations.length} violation(s)`);
  console.error(`scanned: ${scanned}`);
  for (const violation of violations) console.error(`  ✗ ${violation}`);
  process.exit(1);
}

// An empty bank is a pass over nothing. The gate refuses that rather than reporting a green.
if (taskCount === 0 || moduleCount === 0) {
  console.error(`content gate — nothing to validate (scanned: ${scanned})`);
  process.exit(1);
}

console.log(`content gate — OK · scanned: ${scanned}`);
