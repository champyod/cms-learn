# 0014 — The task-authoring form

Status: accepted 2026-11-04 · decision by the operator

## 1. One page, three panels, nothing written until apply

The form produces the generation input described in `docs/task-generation.md`; apply is what creates a
task, and what it creates is a `draft` with `visibility = hidden`, exactly as that document already
requires. The panels are the ones it already names: Plan, Brief, Preview.

## 2. Fourteen controls, two required

Fourteen, not six. The proposal claims the teacher writes six fields in five places
(`intro.typ`, `methodology.typ`, `hypothesis.typ` twice, `scope.typ`); the documented input carries
thirteen keys; the form has fourteen controls because `technique`, `io`, a full-level `constraints` and
the answer contract are all consumed by the pipeline and none of them is a documented key. Two are
required.

| Row | Teacher sets | When blank | Required |
|---|---|---|---|
| technique | a vocabulary node that has a proof template | — | **yes** |
| difficulty | number + rung, one dropdown over both | — | **yes** |
| topics | tags | the technique is the task's only tag | no |
| io | what comes in, what goes out, indexing | the template's | no |
| constraints | the full input bounds | the template's | no |
| answer contract | confirm the template's sentence, or edit it | the template's | no |
| subtasks | count, weights, intent | a rule-valid split from the template | no |
| limits | milliseconds and megabytes | the proof stage derives them | no |
| misdirection | on or off; optionally pin `lean_to` | on, generator chooses `lean_to` | no |
| title | `en` and `th`, generated first | generated | no |
| description | one line for the admin | generated | no |
| locale | languages the task ships | English only | English fixed |
| brief | free text, never published | empty | no |
| cast | people, naming, role | the template's | no |

Not controls: `visibility` is `hidden` on create; `seed` is recorded; and `track` / `stage` are removed
from the admin path entirely — they are parameters of a *participant* generation request (they are in
the cache key), and `0008-no-modules.md` says a task never belongs to a track, so a form that collects
them would be collecting a fact the task is forbidden to have.

## 3. Why technique is a stored field

The template is keyed on one vocabulary node, so the task must record which node that was: `task.json`
gains `technique` (one id) and the gate refuses a task whose `technique` is not among its `topics`. A
technique is therefore always one of the task's tags — the one carrying the template — and the other
tags are labels for placement and search. Nothing else distinguishes the two words.

## 4. The difficulty control

One field drawn as two cells, `2000` and `ค่าย 2`, with no separator between them and one dropdown
covering both; picking a rung writes that band's first step at 100-step granularity. The number is not
typed, so the value and the rung cannot disagree — the same rule as `0013-difficulty-rungs.md`, applied
to the input side.

## 5. Subtasks narrow the bounds

The plan carries the full input bounds once. Each subtask's `constraints` is a narrowing of that set or
absent, and the gate refuses a subtask wider than the full constraints. Exactly one subtask adds no
assumption and it is the highest-weighted one, so "full problem" always has a scoring home. A subtask
whose proving solution passes everything is rejected as a renamed full problem, which is what
`docs/task-generation.md` already says it is.

## 6. Limits: the teacher's pair wins, the proof proves fit

Two sources, one rule:

- A teacher-set pair is what publishes, and the proof stage must prove a correct solution fits inside
  it. If none can, the run is refused with the reason; the limit is never silently widened.
- A blank pair is derived by the proof stage from the intended complexity and the declared bounds. The
  reference solution's run then certifies that a correct solution passes inside it.

The proof stage authors the number in ordinary use; the reference solution is the acceptance test, not
the author. A prediction nobody measured is caught by the certification, and a measurement nobody
predicted is never published.

## 7. Misdirection is on unless switched off

Every generated task reads as if `lean_to` were the intended approach. The teacher may switch it off,
and may pin `lean_to` to a vocabulary node. Two gates already guard it and are unchanged: the decoy must
score above zero and strictly below full, and the statement must not name `lean_to` or the intended
algorithm outside the allowed hint level.

## 8. Title, description, cast

The title is generated in both languages and may be overwritten. The description is admin-only, never
rendered to a learner. Cast is filled from the template and shown read-only in the preview behind an
editor; it bounds the story and never anything a learner is judged on.

## 9. Language: English is mandatory, the rest is chosen

The teacher chooses which languages a task ships; English is required, Thai is optional. The interface
shows the statement in the interface language when the task has it and in English otherwise, with a
short label naming the language it is showing. That is not fallback content — the text is the task's
own statement — so invariant 6 is untouched.

An admin gains **add-language** on a task page, one action per missing language, offering three
sources: upload a file, type the text, or have it generated. It writes that locale's statement and does
**not** bump the version: a translation changes neither what the task asks nor how it is judged, so a
learner's progress keeps pointing at the same task.

## 10. The brief stays, as the document specifies

One optional free-text box, labelled as a directive, never published, never stored beside the
statement, surviving only in the generation audit row. The anti-echo gate still refuses a statement
that copies a span of it. The document calls this the field that produces the failure the pipeline
exists to prevent; it is labelled and gated rather than removed.

## Consequences

- `task.json` gains `technique` and a full-level `constraints`; it already gains `subtasks`, `cast`,
  `misdirection` and `provenance` per `docs/task-generation.md`. The schema, the gate and the falsifier
  each gain one case for `technique ∉ topics` and one for a subtask wider than the full bounds.
- `docs/task-generation.md`'s generation input loses `track` and `stage` on the admin path; that is a
  correction to that document, not a change of behaviour.
- The five places in the proposal that promise six fields are edited to "may set up to six; two are
  required", so the sentence stops being a count and becomes a permission.
- The add-language action gives the task page its first editing control; that page is currently a
  render of the content model only.
- The admin form is not built. This record and `docs/task-generation.md` are the specification.
- Rejected: six mandatory fields (the form would require typing bounds, limits and a subtask split
  every time); per-subtask bounds only (the statement would publish a bound nobody wrote); a stored
  level enum on the task; locale as always-both (a task that ships one language is useful in a draft).
