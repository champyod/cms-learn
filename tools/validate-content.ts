// Content gate.
//
// The bank is data, so the only thing that can go wrong silently is a broken reference: a module
// whose prerequisite was renamed, a task tagged with a topic the vocabulary dropped, a stage that
// names a module that no longer exists, a task marked published while its verification never ran.
// Each of those renders a page that looks correct and teaches something wrong, so this validates
// structure strictly and prints what it scanned — an empty or partial bank must not pass as
// coverage.
import Ajv, { type ErrorObject } from "ajv";
import { existsSync, readFileSync, readdirSync, statSync } from "node:fs";
import { join, relative } from "node:path";

const repoRoot = join(import.meta.dir, "..");
const contentDir = join(repoRoot, "content");

const violations: string[] = [];
const rel = (path: string) => relative(repoRoot, path);

const readJson = <T>(path: string): T | null => {
  try {
    return JSON.parse(readFileSync(path, "utf8")) as T;
  } catch (error) {
    violations.push(`${rel(path)}: unreadable JSON (${(error as Error).message})`);
    return null;
  }
};

const schemaErrors = (path: string, errors: ErrorObject[] | null | undefined): void => {
  for (const error of errors ?? []) {
    const where = error.instancePath.length > 0 ? error.instancePath : "(root)";
    violations.push(`${rel(path)}${where}: ${error.message ?? "invalid"}`);
  }
};

const ajv = new Ajv({ allErrors: true, strict: true });

// ── topic vocabulary ────────────────────────────────────────────────────────────────────────────
interface TopicEntry { id: string; en: string; th: string }
const topicsFile = readJson<{ version: number; topics: TopicEntry[] }>(join(contentDir, "topics.json"));
const vocabulary = new Set<string>();
if (topicsFile) {
  const seen = new Set<string>();
  for (const topic of topicsFile.topics ?? []) {
    if (seen.has(topic.id)) violations.push(`content/topics.json: duplicate topic id "${topic.id}"`);
    seen.add(topic.id);
    vocabulary.add(topic.id);
  }
}

// ── modules ────────────────────────────────────────────────────────────────────────────────────
interface Module {
  id: string;
  area: string;
  title: { en: string; th: string };
  topics: string[];
  prereqs: string[];
}

const modulesPath = join(contentDir, "modules.json");
const modulesFile = readJson<{ version: number; areas: { id: string }[]; modules: Module[] }>(modulesPath);
const moduleIds = new Set<string>();
const moduleTopics = new Map<string, string[]>();
const modulePrereqs = new Map<string, string[]>();
let areaCount = 0;
let moduleCount = 0;

if (modulesFile) {
  const validate = ajv.compile(readJson<object>(join(contentDir, "schema/modules.schema.json")) ?? {});
  if (!validate(modulesFile)) schemaErrors(modulesPath, validate.errors);

  const areaIds = new Set<string>();
  for (const area of modulesFile.areas ?? []) {
    if (areaIds.has(area.id)) violations.push(`modules: duplicate area id "${area.id}"`);
    areaIds.add(area.id);
    areaCount += 1;
  }

  for (const module of modulesFile.modules ?? []) {
    moduleCount += 1;
    if (moduleIds.has(module.id)) violations.push(`modules: duplicate id "${module.id}"`);
    moduleIds.add(module.id);
    moduleTopics.set(module.id, module.topics ?? []);
    modulePrereqs.set(module.id, module.prereqs ?? []);

    if (!areaIds.has(module.area)) violations.push(`module ${module.id}: area "${module.area}" does not exist`);
    for (const topic of module.topics ?? []) {
      if (!vocabulary.has(topic)) violations.push(`module ${module.id}: unknown topic "${topic}"`);
    }
  }

  for (const [moduleId, prereqs] of modulePrereqs) {
    for (const prereq of prereqs) {
      if (!moduleIds.has(prereq)) violations.push(`module ${moduleId}: prerequisite "${prereq}" does not exist`);
    }
  }

  // A prerequisite cycle is an order no student could ever follow.
  const state = new Map<string, "visiting" | "done">();
  const walk = (moduleId: string, path: string[]): void => {
    const current = state.get(moduleId);
    if (current === "done") return;
    if (current === "visiting") {
      violations.push(`modules: prerequisite cycle ${[...path, moduleId].join(" -> ")}`);
      return;
    }
    state.set(moduleId, "visiting");
    for (const prereq of modulePrereqs.get(moduleId) ?? []) walk(prereq, [...path, moduleId]);
    state.set(moduleId, "done");
  };
  for (const moduleId of moduleIds) walk(moduleId, []);
}

// ── tracks ────────────────────────────────────────────────────────────────────────────────────
interface Stage { id: string; title: { en: string; th: string }; modules: string[]; difficulty: { min: number; max: number } }
interface Track { id: string; title: { en: string; th: string }; description: { en: string; th: string }; stages: Stage[] }

const tracksPath = join(contentDir, "tracks.json");
const tracksFile = readJson<{ version: number; tracks: Track[] }>(tracksPath);
const trackedModules = new Set<string>();
let trackCount = 0;
let stageCount = 0;

if (tracksFile) {
  const validate = ajv.compile(readJson<object>(join(contentDir, "schema/tracks.schema.json")) ?? {});
  if (!validate(tracksFile)) schemaErrors(tracksPath, validate.errors);

  const trackIds = new Set<string>();
  const stageIds = new Set<string>();

  for (const track of tracksFile.tracks ?? []) {
    trackCount += 1;
    if (trackIds.has(track.id)) violations.push(`tracks: duplicate track id "${track.id}"`);
    trackIds.add(track.id);

    const inThisTrack = new Set<string>();
    for (const stage of track.stages ?? []) {
      stageCount += 1;
      if (stageIds.has(stage.id)) violations.push(`tracks: duplicate stage id "${stage.id}"`);
      stageIds.add(stage.id);

      if (stage.difficulty.min > stage.difficulty.max) {
        violations.push(`stage ${stage.id}: difficulty band ${stage.difficulty.min}-${stage.difficulty.max} is inverted`);
      }
      for (const moduleId of stage.modules ?? []) {
        if (!moduleIds.has(moduleId)) violations.push(`stage ${stage.id}: module "${moduleId}" does not exist`);
        if (inThisTrack.has(moduleId)) violations.push(`track ${track.id}: module "${moduleId}" appears twice`);
        inThisTrack.add(moduleId);
        trackedModules.add(moduleId);
      }
    }
  }

  // A module no track places is content nobody can be routed to.
  for (const moduleId of moduleIds) {
    if (!trackedModules.has(moduleId)) violations.push(`module ${moduleId}: not placed by any track`);
  }
}

// ── tasks ────────────────────────────────────────────────────────────────────────────────────
interface Task {
  id: string;
  version: number;
  status: "draft" | "verified" | "published" | "rejected";
  topics: string[];
  difficulty: number;
  locales: string[];
  checker: { kind: string; path?: string };
  testdata?: { manifest: string };
  verification: { status: string; reference_solution: string; methods: string[] };
}

const tasksDir = join(contentDir, "tasks");
const taskCounts: Record<string, number> = { draft: 0, verified: 0, published: 0, rejected: 0 };
let taskCount = 0;

/** A task is reachable when some stage's band contains its difficulty and its topics overlap that stage. */
const reachable = (task: Task): boolean => {
  for (const track of tracksFile?.tracks ?? []) {
    for (const stage of track.stages ?? []) {
      if (task.difficulty < stage.difficulty.min || task.difficulty > stage.difficulty.max) continue;
      const stageTopics = new Set(stage.modules.flatMap((moduleId) => moduleTopics.get(moduleId) ?? []));
      if (task.topics.some((topic) => stageTopics.has(topic))) return true;
    }
  }
  return false;
};

if (existsSync(tasksDir)) {
  const validateTask = ajv.compile(readJson<object>(join(contentDir, "schema/task.schema.json")) ?? {});

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

    for (const topic of task.topics ?? []) {
      if (!vocabulary.has(topic)) violations.push(`${rel(taskPath)}: unknown topic "${topic}"`);
    }
    if (entry !== task.id) violations.push(`${rel(taskPath)}: id "${task.id}" does not match its directory "${entry}"`);
    if (!reachable(task)) {
      violations.push(`${rel(taskPath)}: no track stage can serve this task (difficulty ${task.difficulty})`);
    }

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
      for (const method of ["reference-pass", "bruteforce-differential", "limits-measured"]) {
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
  `topics ${vocabulary.size} · areas ${areaCount} · modules ${moduleCount} · tracks ${trackCount} · ` +
  `stages ${stageCount} · tasks ${taskCount} (draft ${taskCounts.draft}, verified ${taskCounts.verified}, ` +
  `published ${taskCounts.published}, rejected ${taskCounts.rejected})`;

if (violations.length > 0) {
  console.error(`content gate — ${violations.length} violation(s)`);
  console.error(`scanned: ${scanned}`);
  for (const violation of violations) console.error(`  ✗ ${violation}`);
  process.exit(1);
}

// An empty bank is a pass over nothing. The gate refuses that rather than reporting a green.
if (taskCount === 0 || moduleCount === 0 || trackCount === 0) {
  console.error(`content gate — nothing to validate (scanned: ${scanned})`);
  process.exit(1);
}

console.log(`content gate — OK · scanned: ${scanned}`);
