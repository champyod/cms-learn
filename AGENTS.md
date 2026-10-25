# AGENTS.md — cms-learn

Repo-level instructions for any agent working in this checkout. Read the hub-root `AGENTS.md` too:
this file only adds what is specific to `cms-learn`.

## What this repo is

A learning platform for competitive programming: one shared task bank, two student-chosen paths
(USACO ladder, IOI path), its own `isolate`-based judge. It is **not** the contest system — nothing
here reads or writes contest data.

`content/` is the single source of truth. Services render it; none of them may hardcode a module, a
topic tag, a track order, a limit, or a string that already exists in the content model.

## Gates — run before every commit that touches content, and before claiming any lane green

```bash
bun install                # once, or after package.json changes
bun run validate           # content integrity; prints the counts it scanned
bun run validate:falsify   # proves the gate can fail; must print "N case(s) behaved, 0 did not"
```

A green `validate` with no `falsify` run is half the evidence. If you teach the gate a new defect
class, add its falsification case in the same commit — the harness is what proves the rule bites.

For any reference solution you add: `g++ -O2 -std=c++17 -Wall -Wextra`, then run it against the
example printed in its own statement. A reference that cannot reproduce its statement's example is a
broken task, not a formatting issue.

## Adding work

- One intent per commit. The message carries the **why** and, on a branch's first commit, the base it
  was cut from.
- A change to the content model goes in `docs/decisions/NNNN-<slug>.md` **in the same commit** as the
  code, with what was chosen, why, and what was rejected. A model change with no ADR is a change the
  next reader will undo.
- Status and counts live in the hub's `epics/003-cms-learn.md` and in `FEATURES.md`. When a feature
  lands, move its row — do not start a parallel list.

## Invariants (do not "fix" these)

1. **No level on a task or a module.** Tracks rank modules; tasks carry `topics` + `difficulty`.
   Putting a level back makes one task rank differently per track and duplicates the corpus.
2. **Reachability is checked.** A task no track stage can serve fails the gate. That is the point,
   not a bug to work around.
3. **`published` requires machine evidence.** Never hand-edit a task to `published`, and never
   weaken the verification rule to make a bank pass.
4. **One token source.** `web/tokens.css`. Tailwind or any component library maps to those
   variables; a second palette file is the duplicated-design-system defect.
5. **Thai rules:** no uppercase-as-hierarchy, no italic synthesis, body line-height ≥ 1.45, and the
   mono font is never subset by `unicode-range` (Latin would fall back to the system mono and mixed
   Thai/Latin columns drift).
6. **No fallback content.** A missing statement or an unverified task must fail loudly, never render
   a placeholder a student would read as real material.

## Never

- Invent endpoints, commands or screens. If it is not in the tree it does not exist — write "not
  built" instead of a plausible description.
- Install a host package, add a host script, or introduce a host environment variable. Automation
  ships as a container service or a CI job; the only permitted host interaction is a read-only mount.
- Commit a font, model or other vendored binary without its licence text beside it.
- Delete or weaken a falsification case to reach green.

## Where things live

| Path | Holds |
|---|---|
| `content/topics.json` | controlled topic vocabulary (id + TH/EN labels) |
| `content/modules.json` | areas + modules (`topics`, `prereqs`) |
| `content/tracks.json` | tracks → stages (`modules`, `difficulty` band) |
| `content/tasks/<slug>/` | `task.json`, one statement per locale, reference solution |
| `content/schema/` | JSON Schemas — strict, `additionalProperties: false` |
| `tools/validate-content.ts` | the content gate |
| `tools/falsify-content-gate.sh` | proof that the gate can fail |
| `web/tokens.css` | design tokens (`judge mono`) |
| `design/` | static previews: chosen direction, twelve-row gallery |
| `docs/decisions/` | ADRs |
| `docs/content-model.md` | the model, the cache key, the serving policy |
| `FEATURES.md` | per-feature registry, shipped vs planned |
