import { spawnSync } from "node:child_process";
import { appendFileSync, existsSync, lstatSync, mkdirSync, readFileSync, realpathSync, rmSync, writeFileSync } from "node:fs";
import { dirname, isAbsolute, join, sep } from "node:path";

import type { JsonEdit, JsonPath, JsonValue, Plant } from "./contract.ts";
import { firstLine, reason } from "./text.ts";

type JsonRef = { kind: "array"; container: JsonValue[]; index: number } | { kind: "object"; container: Record<string, JsonValue>; key: string };

const isRecord = (value: JsonValue): value is Record<string, JsonValue> => typeof value === "object" && value !== null && !Array.isArray(value);
const readRef = (ref: JsonRef): JsonValue => (ref.kind === "array" ? ref.container[ref.index] : ref.container[ref.key]);

const lexists = (path: string): boolean => {
  try { lstatSync(path); return true; } catch { return false; }
};

// `link` makes a symlink into the checkout reachable, so containment is checked on the real path, not the joined one.
const stagedPath = (root: string, path: string): string => {
  if (isAbsolute(path) || path.split("/").includes("..")) throw new Error(`plant path ${path} leaves the staged tree`);
  const resolvedRoot = realpathSync(root);
  const target = join(resolvedRoot, path);
  let probe = target;
  while (probe !== resolvedRoot && !lexists(probe)) probe = dirname(probe);
  let resolved: string;
  try { resolved = realpathSync(probe); } catch (error) { throw new Error(`plant path ${path} is a broken symlink (${reason(error)})`); }
  if (resolved !== resolvedRoot && !resolved.startsWith(`${resolvedRoot}${sep}`)) throw new Error(`plant path ${path} resolves outside the staged tree`);
  return target;
};

function plantWrite(plant: { kind: "write"; path: string; content: string }, root: string): void {
  const target = stagedPath(root, plant.path);
  mkdirSync(dirname(target), { recursive: true });
  writeFileSync(target, plant.content);
}

function plantAppend(plant: { kind: "append"; path: string; content: string }, root: string): void {
  const target = stagedPath(root, plant.path);
  if (!existsSync(target)) throw new Error(`cannot append to ${plant.path}: it is not in the staged tree`);
  appendFileSync(target, plant.content);
}

function plantPatch(plant: { kind: "patch"; path: string; find: string; replace: string }, root: string): void {
  const target = stagedPath(root, plant.path);
  if (!existsSync(target)) throw new Error(`cannot patch ${plant.path}: it is not in the staged tree`);
  const text = readFileSync(target, "utf8");
  const found = text.split(plant.find).length - 1;
  if (found !== 1) throw new Error(`cannot patch ${plant.path}: the pattern occurs ${found} time(s), expected exactly 1`);
  writeFileSync(target, text.split(plant.find).join(plant.replace));
}

function plantReplaceAll(plant: { kind: "replaceAll"; path: string; find: string; replace: string; matches: number }, root: string): void {
  const target = stagedPath(root, plant.path);
  if (!existsSync(target)) throw new Error(`cannot replace in ${plant.path}: it is not in the staged tree`);
  const text = readFileSync(target, "utf8");
  const found = text.split(plant.find).length - 1;
  if (found !== plant.matches) throw new Error(`cannot replace in ${plant.path}: the pattern occurs ${found} time(s), expected ${plant.matches}`);
  writeFileSync(target, text.split(plant.find).join(plant.replace));
}

function plantRemove(plant: { kind: "remove"; path: string }, root: string): void {
  const target = stagedPath(root, plant.path);
  if (!existsSync(target)) throw new Error(`cannot remove ${plant.path}: it is not in the staged tree`);
  rmSync(target, { recursive: true });
}

const runBash = (script: string, root: string, env: NodeJS.ProcessEnv): { status: number; output: string } => {
  const result = spawnSync("bash", ["-c", script], { cwd: root, env, encoding: "utf8", maxBuffer: 16 * 1024 * 1024 });
  if (result.error !== undefined) throw new Error(`cannot run bash -c: ${result.error.message}`);
  return { status: result.status ?? 1, output: `${result.stdout ?? ""}${result.stderr ?? ""}` };
};

function plantRun(plant: { kind: "run"; script: string; verify: string }, root: string, env: NodeJS.ProcessEnv): void {
  const script = runBash(plant.script, root, env);
  if (script.status !== 0) throw new Error(`plant script failed: exit ${script.status}: ${firstLine(script.output)}`);
  const witness = runBash(plant.verify, root, env);
  if (witness.status !== 0) {
    throw new Error(`plant witness failed: exit ${witness.status} for "${plant.verify}": ${firstLine(witness.output)}`);
  }
}

function selectRefs(node: JsonValue, segment: JsonPath[number], where: string): JsonRef[] {
  if (segment === "*") {
    if (Array.isArray(node)) return node.map((_, index): JsonRef => ({ kind: "array", container: node, index }));
    if (isRecord(node)) return Object.keys(node).map((key): JsonRef => ({ kind: "object", container: node, key }));
    throw new Error(`${where}: "*" needs an array or an object`);
  }
  if (typeof segment === "object") {
    const items = Array.isArray(node) ? node : [];
    const refs = items.flatMap((item, index): JsonRef[] => (isRecord(item) && item[segment.match] === segment.equals ? [{ kind: "array", container: items, index }] : []));
    if (!Array.isArray(node) || refs.length !== 1) throw new Error(`${where}: ${segment.match}=${segment.equals} matched ${refs.length} node(s), expected exactly 1`);
    return refs;
  }
  if (typeof segment === "number") {
    if (!Array.isArray(node) || segment < 0 || segment >= node.length) throw new Error(`${where}: index ${segment} does not exist`);
    return [{ kind: "array", container: node, index: segment }];
  }
  if (!isRecord(node) || !(segment in node)) throw new Error(`${where}: key ${segment} does not exist`);
  return [{ kind: "object", container: node, key: segment }];
}

function resolveRefs(document: JsonValue, pointer: JsonPath, where: string): JsonRef[] {
  const holder: Record<string, JsonValue> = { root: document };
  let refs: JsonRef[] = [{ kind: "object", container: holder, key: "root" }];
  for (const segment of pointer) refs = refs.flatMap((ref) => selectRefs(readRef(ref), segment, where));
  if (refs.length === 0) throw new Error(`${where}: the pointer resolved to no nodes`);
  return refs;
}

function deepEqual(left: JsonValue, right: JsonValue): boolean {
  if (left === right) return true;
  if (Array.isArray(left) && Array.isArray(right)) return left.length === right.length && left.every((item, index) => deepEqual(item, right[index]));
  if (!isRecord(left) || !isRecord(right)) return false;
  const keys = Object.keys(left);
  return keys.length === Object.keys(right).length && keys.every((key) => key in right && deepEqual(left[key], right[key]));
}

function appendTo(ref: JsonRef, value: JsonValue, where: string): void {
  const target = readRef(ref);
  if (!Array.isArray(target)) throw new Error(`${where}: the pointer must resolve to an array`);
  target.push(value);
}

function removeFrom(ref: JsonRef, value: JsonValue, where: string): void {
  const target = readRef(ref);
  if (!Array.isArray(target)) throw new Error(`${where}: the pointer must resolve to an array`);
  const kept = target.filter((item) => !deepEqual(item, value));
  if (kept.length === target.length) throw new Error(`${where}: removeValue matched no element`);
  target.splice(0, target.length, ...kept);
}

function applyJsonEdit(document: JsonValue, edit: JsonEdit, where: string): JsonValue {
  if (edit.kind === "set") {
    const refs = resolveRefs(document, edit.pointer, where);
    if (refs.length !== 1) throw new Error(`${where}: set needs exactly one resolved node, got ${refs.length}`);
    const ref = refs[0];
    if (ref.kind === "array") ref.container[ref.index] = edit.value;
    else ref.container[ref.key] = edit.value;
    return document;
  }
  if (edit.kind === "appendCopy") {
    const source = resolveRefs(document, edit.source, `${where} source`);
    if (source.length !== 1) throw new Error(`${where}: appendCopy needs exactly one source node, got ${source.length}`);
    const value = readRef(source[0]);
    for (const ref of resolveRefs(document, edit.pointer, where)) appendTo(ref, value, where);
    return document;
  }
  for (const ref of resolveRefs(document, edit.pointer, where)) {
    if (edit.kind === "append") appendTo(ref, edit.value, where);
    else removeFrom(ref, edit.value, where);
  }
  return document;
}

function plantJson(plant: { kind: "json"; path: string; edits: readonly JsonEdit[] }, root: string): void {
  const target = stagedPath(root, plant.path);
  if (!existsSync(target)) throw new Error(`cannot edit ${plant.path}: it is not in the staged tree`);
  let document: JsonValue;
  try { document = JSON.parse(readFileSync(target, "utf8")); } catch (error) { throw new Error(`cannot edit ${plant.path}: not valid JSON (${reason(error)})`); }
  for (const edit of plant.edits) document = applyJsonEdit(document, edit, `${plant.path} ${edit.kind}`);
  writeFileSync(target, `${JSON.stringify(document, null, 2)}\n`);
}

/** Applies one plant in order; a plant that cannot be applied exactly as stated throws naming the file and reason. */
export function applyPlant(plant: Plant, root: string, env: NodeJS.ProcessEnv): void {
  switch (plant.kind) {
    case "write": return plantWrite(plant, root);
    case "append": return plantAppend(plant, root);
    case "patch": return plantPatch(plant, root);
    case "replaceAll": return plantReplaceAll(plant, root);
    case "remove": return plantRemove(plant, root);
    case "json": return plantJson(plant, root);
    case "run": return plantRun(plant, root, env);
  }
}
