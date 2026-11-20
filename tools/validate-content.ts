// Content gate.
//
// The bank is data, so the only thing that can go wrong silently is a broken reference: a stage
// whose prerequisite was renamed, a task tagged with a topic the vocabulary dropped, a stage that
// names a tag that no longer exists, a task marked published while its verification never ran.
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

// ── locales ──────────────────────────────────────────────────────────────────────────────────
/// The languages a task may ship, with the name a prompt says (ADR 0031 §2: the code knows no language, so the
/// name arrives as data). A task's `locales` entry and its statement file both have to be declared here.
interface LocaleEntry { tag: string; name: string }

const localesPath = join(contentDir, "locales.json");
const localesFile = readJson<{ version: number; locales: LocaleEntry[] }>(localesPath);
const locales = new Map<string, string>();
let localeCount = 0;

if (localesFile) {
  const validateLocales = ajv.compile(readJson<object>(join(contentDir, "schema/locales.schema.json")) ?? {});
  if (!validateLocales(localesFile)) {
    schemaErrors(localesPath, validateLocales.errors);
  }
  const seen = new Set<string>();
  for (const locale of localesFile.locales ?? []) {
    if (seen.has(locale.tag)) violations.push(`${rel(localesPath)}: duplicate locale tag "${locale.tag}"`);
    seen.add(locale.tag);
    if (locale.name.trim().length === 0) {
      violations.push(`${rel(localesPath)}: locale "${locale.tag}" carries no display name`);
    }
    locales.set(locale.tag, locale.name);
    localeCount += 1;
  }
}

// ── levels ───────────────────────────────────────────────────────────────────────────────────
interface Level { id: string; title: { en: string }; min: number; max: number }

const SCALE_MIN = 800;
const SCALE_MAX = 3500;

const levelsPath = join(contentDir, "levels.json");
const levelsFile = readJson<{ version: number; levels: Level[] }>(levelsPath);
let levelCount = 0;

if (levelsFile) {
  const validate = ajv.compile(readJson<object>(join(contentDir, "schema/levels.schema.json")) ?? {});
  if (!validate(levelsFile)) schemaErrors(levelsPath, validate.errors);

  const levelIds = new Set<string>();
  const ordered = [...(levelsFile.levels ?? [])].sort((left, right) => left.min - right.min);

  for (const level of ordered) {
    levelCount += 1;
    if (levelIds.has(level.id)) violations.push(`levels: duplicate level id "${level.id}"`);
    levelIds.add(level.id);
    if (level.min > level.max) {
      violations.push(`level ${level.id}: band ${level.min}-${level.max} is inverted`);
    }
  }

  // The bands must partition the scale: a task outside every band would have no level to show, and an
  // overlap would let one difficulty carry two, which is the ambiguity the whole rule exists to remove.
  let expected = SCALE_MIN;
  for (const level of ordered) {
    if (level.min !== expected) {
      violations.push(`level ${level.id}: starts at ${level.min}, but the scale expects ${expected}`);
    }
    expected = level.max + 100;
  }
  if (ordered.length > 0 && ordered[ordered.length - 1].max !== SCALE_MAX) {
    violations.push(`levels: the last band ends at ${ordered[ordered.length - 1].max}, not ${SCALE_MAX}`);
  }
}

// ── tracks ────────────────────────────────────────────────────────────────────────────────────
interface Stage { id: string; title: { en: string; th: string }; tags: string[]; difficulty: { min: number; max: number } }
interface Track { id: string; title: { en: string; th: string }; description: { en: string; th: string }; stages: Stage[] }

const tracksPath = join(contentDir, "tracks.json");
const tracksFile = readJson<{ version: number; tracks: Track[] }>(tracksPath);
const trackedTags = new Set<string>();
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

    for (const stage of track.stages ?? []) {
      stageCount += 1;
      if (stageIds.has(stage.id)) violations.push(`tracks: duplicate stage id "${stage.id}"`);
      stageIds.add(stage.id);

      if (stage.difficulty.min > stage.difficulty.max) {
        violations.push(`stage ${stage.id}: difficulty band ${stage.difficulty.min}-${stage.difficulty.max} is inverted`);
      }
      // The same skill may be revisited at a harder band, so a tag repeats across a track's stages
      // and must not repeat inside one stage.
      const inThisStage = new Set<string>();
      for (const tag of stage.tags ?? []) {
        if (!vocabulary.has(tag)) violations.push(`stage ${stage.id}: tag "${tag}" does not exist`);
        if (inThisStage.has(tag)) violations.push(`stage ${stage.id}: tag "${tag}" appears twice`);
        inThisStage.add(tag);
        trackedTags.add(tag);
      }
    }
  }

  // Both routes are views of the same bank, so each one must place every tag: a tag missing from a
  // route is a skill that route silently cannot show.
  for (const track of tracksFile.tracks ?? []) {
    const placed = new Set<string>((track.stages ?? []).flatMap((stage) => stage.tags ?? []));
    for (const tag of vocabulary) {
      if (!placed.has(tag)) violations.push(`track ${track.id}: tag "${tag}" is placed by no stage`);
    }
  }
}

// ── tag prerequisites ────────────────────────────────────────────────────────────────────────
// The graph view draws these edges, so a wrong one teaches a student to learn a topic before the
// thing it rests on. The failure that hides is structural — a prerequisite id that no longer
// exists, a tag dropped from the vocabulary, an edge that loops back on itself, or a chain that
// closes into a cycle and leaves no honest place to start — so the graph is checked as a graph.
interface PrereqEdge { tag: string; prereqs: string[]; origin: string; evidence: string }
const prereqsPath = join(contentDir, "prereqs.json");
const prereqsFile = readJson<{ version: number; edges: PrereqEdge[] }>(prereqsPath);
const allowedOrigins = new Set(["usaco-guide", "authored"]);
let prereqUsacoEdges = 0;
let prereqAuthoredEdges = 0;
let prereqTagsWithEdges = 0;
let prereqTagCount = 0;

if (prereqsFile) {
  const graph = new Map<string, string[]>();
  for (const edge of prereqsFile.edges ?? []) {
    if (typeof edge?.tag !== "string") {
      violations.push(`content/prereqs.json: entry without a tag key`);
      continue;
    }
    const tag = edge.tag;
    prereqTagCount += 1;
    if (graph.has(tag)) violations.push(`prereqs: tag "${tag}" appears more than once`);
    graph.set(tag, edge.prereqs ?? []);
    if (!vocabulary.has(tag)) violations.push(`prereqs: tag "${tag}" does not exist in the vocabulary`);
    if (!allowedOrigins.has(edge.origin)) {
      violations.push(`prereqs ${tag}: origin "${edge.origin}" is neither usaco-guide nor authored`);
    }
    // An edge we attribute to USACO Guide must say which module declared it, or the next reader
    // has to take our word for a claim that came from someone else.
    if (edge.origin === "usaco-guide" && (edge.evidence ?? "").trim().length === 0) {
      violations.push(`prereqs ${tag}: usaco-guide edge with no evidence`);
    }
    if ((edge.prereqs ?? []).length > 0) prereqTagsWithEdges += 1;
    if (edge.origin === "usaco-guide") prereqUsacoEdges += (edge.prereqs ?? []).length;
    if (edge.origin === "authored") prereqAuthoredEdges += (edge.prereqs ?? []).length;

    const inThisTag = new Set<string>();
    for (const prereq of edge.prereqs ?? []) {
      if (!vocabulary.has(prereq)) violations.push(`prereqs ${tag}: prerequisite "${prereq}" does not exist`);
      if (prereq === tag) violations.push(`prereqs ${tag}: lists itself as a prerequisite`);
      if (inThisTag.has(prereq)) violations.push(`prereqs ${tag}: prerequisite "${prereq}" appears twice`);
      inThisTag.add(prereq);
    }
  }

  // Every tag carries an entry even when it is a root: a tag missing from the file is a tag the
  // graph cannot draw at all, which looks the same as a topic nobody has to prepare for.
  for (const tag of vocabulary) {
    if (!graph.has(tag)) violations.push(`prereqs: tag "${tag}" has no entry`);
  }

  const reported = new Set<string>();
  const state = new Map<string, "visiting" | "done">();
  const walk = (tag: string, trail: string[]): void => {
    state.set(tag, "visiting");
    for (const prereq of graph.get(tag) ?? []) {
      if (!graph.has(prereq)) continue;
      if (state.get(prereq) === "visiting") {
        const cycle = [...trail.slice(trail.indexOf(prereq)), tag, prereq];
        const signature = [...cycle].sort().join(",");
        if (!reported.has(signature)) {
          reported.add(signature);
          violations.push(`prereqs: cycle ${cycle.join(" -> ")}`);
        }
        continue;
      }
      if (state.get(prereq) === "done") continue;
      walk(prereq, [...trail, tag]);
    }
    state.set(tag, "done");
  };
  for (const tag of graph.keys()) {
    if (!state.has(tag)) walk(tag, []);
  }
}

// ── tasks ────────────────────────────────────────────────────────────────────────────────────
interface Subtask {
  position: number;
  points: number;
  min_n?: number;
  max_n?: number;
  value_min?: number;
  value_max?: number;
}

interface Task {
  id: string;
  version: number;
  status: "draft" | "published" | "rejected";
  topics: string[];
  technique: string;
  difficulty: number;
  locales: string[];
  checker: { kind: string; path?: string };
  bounds?: { min_n: number; max_n: number; value_min: number; value_max: number };
  subtasks?: Subtask[];
  testdata?: { manifest: string };
  verification: { reference_solution: string };
}

/** A subtask's constraint is a typed narrowing of the task's own bounds (ADR 0041), never a sentence. */
const subtaskFaults = (task: Task): string[] => {
  const faults: string[] = [];
  const subtasks = task.subtasks ?? [];
  if (subtasks.length === 0) return faults;
  const bounds = task.bounds;
  if (!bounds) {
    return ["a subtask narrows the task's bounds, and the task declares none"];
  }
  const seen = new Set<number>();
  for (const subtask of subtasks) {
    const at = `subtask ${subtask.position}`;
    if (seen.has(subtask.position)) faults.push(`${at}: position is named twice`);
    seen.add(subtask.position);
    const fields = ["min_n", "max_n", "value_min", "value_max"] as const;
    const named = fields.filter((field) => subtask[field] !== undefined);
    if (named.length === 0) {
      faults.push(`${at}: narrows nothing, so it is the whole task and earns no points of its own`);
      continue;
    }
    for (const field of named) {
      const value = subtask[field] as number;
      const isLower = field === "min_n" || field === "value_min";
      if (isLower ? value < bounds[field] : value > bounds[field]) {
        faults.push(`${at}: ${field} ${value} is ${isLower ? "below" : "above"} the task's ${bounds[field]}`);
      }
    }
    const minN = subtask.min_n ?? bounds.min_n;
    const maxN = subtask.max_n ?? bounds.max_n;
    if (minN > maxN) faults.push(`${at}: admits no n, since min_n ${minN} is above max_n ${maxN}`);
    const valueMin = subtask.value_min ?? bounds.value_min;
    const valueMax = subtask.value_max ?? bounds.value_max;
    if (valueMin > valueMax) {
      faults.push(`${at}: admits no value, since value_min ${valueMin} is above value_max ${valueMax}`);
    }
  }
  return faults;
};

/** ADR 0046 §2 retired the file's verification claim: what ran is the certificate row's evidence, so a file that
 * still names a status, the methods, or a verification time is refused rather than read as verified. */
const RETIRED_VERIFICATION_KEYS = new Set(["status", "methods", "verified_at"]);

const retiredClaimFaults = (task: Task): string[] => {
  const named = Object.keys(task.verification ?? {}).filter((key) => RETIRED_VERIFICATION_KEYS.has(key));
  if (named.length === 0) return [];
  return [
    `verification names ${named.join(", ")}, which ADR 0046 §2 retired: a task's verified state is the certificate row's, so the file cannot say it`,
  ];
};

const tasksDir = join(contentDir, "tasks");
const taskCounts: Record<string, number> = { draft: 0, published: 0, rejected: 0 };
let taskCount = 0;

/** A task is reachable when some stage's band contains its difficulty and its topics overlap that stage. */
const reachable = (task: Task): boolean => {
  for (const track of tracksFile?.tracks ?? []) {
    for (const stage of track.stages ?? []) {
      if (task.difficulty < stage.difficulty.min || task.difficulty > stage.difficulty.max) continue;
      const stageTopics = new Set(stage.tags ?? []);
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
    for (const fault of retiredClaimFaults(task)) violations.push(`${rel(taskPath)}: ${fault}`);
    for (const fault of subtaskFaults(task)) violations.push(`${rel(taskPath)}: ${fault}`);

    for (const topic of task.topics ?? []) {
      if (!vocabulary.has(topic)) violations.push(`${rel(taskPath)}: unknown topic "${topic}"`);
    }
    // The technique selects the proof template, so it has to be one of the task's own tags.
    if (!(task.topics ?? []).includes(task.technique)) {
      violations.push(`${rel(taskPath)}: technique "${task.technique}" is not one of its topics`);
    }
    if (entry !== task.id) violations.push(`${rel(taskPath)}: id "${task.id}" does not match its directory "${entry}"`);
    if (!reachable(task)) {
      violations.push(`${rel(taskPath)}: no track stage can serve this task (difficulty ${task.difficulty})`);
    }

    for (const locale of task.locales ?? []) {
      if (!locales.has(locale)) {
        violations.push(`${rel(taskPath)}: locale "${locale}" is not declared in content/locales.json`);
      }
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

    // A published task is what a student can be served. The file cannot say a task is verified (ADR 0046 §2): the
    // certificate row and the publisher decide that, so the gate proves only that the content the row will certify
    // is here.
    if (task.status === "published") {
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
  `topics ${vocabulary.size} · locales ${localeCount} · tracks ${trackCount} · ` +
  `stages ${stageCount} · levels ${levelCount} · tasks ${taskCount} (draft ${taskCounts.draft}, ` +
  `published ${taskCounts.published}, rejected ${taskCounts.rejected}) · ` +
  `prereqs ${prereqTagCount} tag(s), ${prereqTagsWithEdges} with edges ` +
  `(${prereqUsacoEdges} usaco-guide, ${prereqAuthoredEdges} authored)`;

if (violations.length > 0) {
  console.error(`content gate — ${violations.length} violation(s)`);
  console.error(`scanned: ${scanned}`);
  for (const violation of violations) console.error(`  ✗ ${violation}`);
  process.exit(1);
}

// An empty bank is a pass over nothing. The gate refuses that rather than reporting a green.
if (taskCount === 0 || trackCount === 0) {
  console.error(`content gate — nothing to validate (scanned: ${scanned})`);
  process.exit(1);
}

console.log(`content gate — OK · scanned: ${scanned}`);
