# 0013 — The difficulty a learner works at

Status: accepted 2026-11-04 · decision by the operator

## 1. The number stays the only stored difficulty

`difficulty` is one integer, 800–3500, step 100 — `content/schema/task.schema.json`, with the same
constraint in the database (`deploy/plan/0001_init.json`). That range and step are the Codeforces
rating scale, so "normalized to Codeforces" needed no change to the data; it was already that shape.

What was missing was a name. A learner reading `1800` has no local reference, and the syllabus the
platform teaches to already publishes one.

## 2. The four rungs are the syllabus's own columns, not ours

`seed/vocab/raw/posn.json` is the Thai POSN computer-olympiad syllabus 2569 (posn.or.th, fetched
2026-10-29). It records every syllabus row against the stage column the row is ticked in, and exactly
four columns exist:

| Rung | Source column | Rows |
|---|---|---|
| TOI-Zero | `toi-zero` | 26 |
| ค่าย 1 | `camp-1` | 52 |
| ค่าย 2 | `camp-2` | 90 |
| TOI–IOI | `toi-ioi` | 97 |

This is why the names may exist at all. `0007-tag-vocabulary.md` §6 forbids inventing Thai, and these
four are not invented — they are the columns of a document this repository already stores, in the
programme's own order. The entry round is named after itself (TOI-Zero), not after the contest it
leads to, so the easiest rung is not labelled with the contest's name.

## 3. A rung is a function of the number, never a stored field

The four rungs cut 800–3500 at three thresholds:

| Rung | Band |
|---|---|
| TOI-Zero | 800–1400 |
| ค่าย 1 | 1500–1800 |
| ค่าย 2 | 1900–2400 |
| TOI–IOI | 2500–3500 |

The bands are contiguous, so every difficulty belongs to exactly one rung and a task can carry one
level badge without ambiguity. The thresholds are a decision, not a measurement of what these contests
contain; they are written here because they have to be defensible and changeable in one place. Nothing
computes a number from a rung — the rung is computed from the number.

## 4. The account stores a number too

A learner's chosen rung is `principals.difficulty_target`: a nullable integer carrying the same
800–3500/step-100 constraint, `NULL` meaning no rung chosen. The chip derives its rung from that
number, so an account obeys the same rule as a task.

Why not store the rung id: a stored rung is a second vocabulary, and moving one threshold would become
a data migration instead of an edit to this record. Why not a per-account row in `settings`: a setting
is an operator value by construction (`docs/settings.md`), and this is a learner's own working level.

The level is always changeable — no approval, no schedule, no reset — including upwards, so a learner
who wants harder work can take it.

## 5. One control, in the navigation, in two shapes

Desktop: a four-segment meter and the rung name, immediately before the language switch; the popover
lists the four rungs with their numeric bands and marks the current one.

Below the small breakpoint: the meter alone as a square, opening the same list as a sheet. The control
never moves inside the account menu: the level is a mode a learner changes while working, not a
profile fact, and one more tap on the most-used control is the wrong trade.

The meter rather than a crown because the icon's job is to say *which* rung, and a single glyph says
none of it. No emoji: an emoji glyph falls outside the Thai/Latin monospace metrics the token layer
exists to guarantee (`0002-theme-and-auth.md`).

## 6. Why this is not the level `AGENTS.md` invariant 1 forbids

Invariant 1 says a task carries topics and difficulty, never a level, and `0001-scope-stack-sandbox.md`
§3 records why: a level stored on a task lets one task rank differently per track and duplicates the
corpus.

Nothing here is stored and nothing is per-track. A rung is a total function of `difficulty` through
three fixed thresholds, so two tasks at the same difficulty receive the same rung in every track, every
locale and every query. There is no field that can disagree, no column to migrate when a threshold
moves, and no way for a route to change a task's rank. The badge on a task page is computed at render
time exactly as the numeric label is today.

If a rung is ever stored on a task, or made to depend on the track, this record is void and invariant 1
applies as written.

## 7. What this does not change

- No stored level on a task. `docs/content-model.md` keeps its "no second difficulty vocabulary" rule,
  and a rung cannot disagree with a number because it is derived from one.
- No new ordering for serving. Eligibility stays the route's stage band (`0008-no-modules.md`); the
  rung adds a label and a filter, not a third ordering.
- The generation cache key is unchanged: `difficulty` was already in it.
- The proposal document is unchanged. It names the difficulty as one of the six brief fields and names
  no contest ladder; adding the rungs there is a separate decision, and under `FACTS.md` a
  band-to-rung mapping would enter as a decision rather than a fact.

## Consequences

- One migration adds one nullable column. No backfill: `NULL` is already correct for every account.
- This is the first per-account preference, the first UI icon (an inline SVG sprite — the application
  ships only logo SVGs today), and the first navbar control that is not a link.
- The all-tasks page does not exist yet; `api/src/page_routes.cpp` registers `/`, `/tasks/{}` and
  `/settings`. The chip's "filter the list" behaviour is specified here and built there.
- The syllabus already records which topics each rung teaches, per row. Nothing reads that today; it is
  the natural input for the prerequisite graph page, and that use is not decided here.
- Rejected: a stored `level` enum on the task (two descriptions that can disagree); ten Codeforces
  titles as labels (no Thai source, so `th: null` under 0007 §6); overlapping rung bands (a task would
  belong to two levels, so its badge would have to lie or vanish).
