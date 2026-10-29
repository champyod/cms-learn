# Task generation

A task is generated as a **draft** and served only after the verification in `docs/content-model.md`
passes. Generation is a factory, not an authority: nothing it produces is trusted, and the admin page
is a control surface over the same pipeline a build would use.

## Three kinds of text, never interchangeable

The failure this document exists to prevent is a model that treats an instruction as content. An admin
writes what they want in prose; a model that does not distinguish "this tells you what to build" from
"this is the text to publish" puts the instruction in front of students.

| Kind | Who reads it | Where it lives | May appear in the statement |
|---|---|---|---|
| **Brief** | the model, as a directive | the generation request, audited | never |
| **Description** | the model as context in later calls; the admin for identification | the task row, admin-only | never |
| **Plan** | the model and every gate | the task row, structured | never — but its numbers must be satisfied |
| **Statement** and **story** | students | `content/` | it *is* the statement |

The brief says *what the admin wants*. The plan says *what is objectively true about the task*: topics,
difficulty, limits, subtask constraints, the decoy target, the cast rules, the locale. Only the plan is
checkable, so the plan is what the gates read.

The brief is never stored beside the statement, and never rendered. It survives in the generation audit
row — what was asked, with which model and seed — because a task whose origin cannot be reconstructed
is a task nobody can defend.

## Two generation paths, and they must not share a cache

| | Participant path | Admin path |
|---|---|---|
| Trigger | a student's path needs work nobody has written | an admin authors a task deliberately |
| Lookup | the cache key in `docs/content-model.md`; an existing artifact is reused for every account | **no lookup** — generation is direct and creates a new task every time |
| Default visibility | `shared` once published | **`hidden`** |
| Output | a task for the serving pool | a draft for a lesson, an exam, or the pack |

The cache exists to stop N students causing N generations of the same task. An admin is not a student:
reusing a cached artifact would hand them a task shaped by someone else's brief, and the cache key
cannot express a brief. So the admin path ignores the cache entirely and always generates.

## Visibility

`status` is about quality; `visibility` is about eligibility. They are orthogonal on purpose.

| Field | Values | Meaning |
|---|---|---|
| `status` | `draft` · `verified` · `published` · `rejected` | has it passed machine evidence |
| `visibility` | `hidden` · `shared` | may the serving path ever select it |

A task is served only when `status = published` **and** `visibility = shared`. Admin generation sets
`hidden`, and an admin marks a task `shared` later, deliberately, after using it.

`hidden` means three enforced things, not one label:

1. **Not served** — the serving query in `docs/content-model.md` filters on `visibility = shared`, so a
   hidden task cannot reach a student by any path.
2. **Not reused** — a hidden task is never returned as a cached artifact and never enters the pool a
   participant generation searches.
3. **Not counted** — a hidden task does not satisfy "unseen" bookkeeping, so marking one shared later
   cannot silently rewrite progress for accounts that never saw it.

Why default to hidden rather than to shared: a freshly authored item has no track record. Exposing it
immediately is how one bad task reaches a whole cohort before anyone reads it, and how a good task
gets reused into irrelevance — both are documented failure modes of item exposure, below.

## The generation input

```
{
  "brief":            "free text from the admin — a directive, not text to publish",
  "description":      "one line identifying the task for an admin",
  "locale":           ["th", "en"],
  "track":            "ioi",
  "stage":            "graph-2",
  "topics":           ["graph", "greedy"],
  "difficulty":       1800,
  "limits":           { "time_ms": 1000, "memory_mb": 256 },
  "subtasks":         [ { "intent": "greedy", "weight": 30 }, { "intent": "greedy+dsu", "weight": 70 } ],
  "misdirection":     { "enabled": true, "lean_to": "dp", "phrasing": "manual override or null" },
  "cast":             { "people": 2, "naming": "th-en-paired", "role": "student contest" },
  "visibility":       "hidden",
  "seed":             20261029
}
```

`seed` is recorded, not chosen at random per call: the same input must produce the same task, or the
cache key in `docs/content-model.md` describes something that cannot be reproduced.

## The generation output

### Storage

A task is a **row plus blobs**. The database holds the metadata, the plan, the visibility, the
subtasks, the provenance and the pointers; an S3-compatible object store (SeaweedFS) holds every byte of text,
source and test data, reached from the core by aws-sdk-cpp's `s3` feature. There is no task directory on a host, and no path in the database that names one.

```
tasks/<task_id>/<version>/<sha256>/statement.en.md
tasks/<task_id>/<version>/<sha256>/statement.th.md
tasks/<task_id>/<version>/<sha256>/tests/<group>.tar.zst
…
```

Keys are **content-addressed**: the digest is the object's name, so a corrected file is a new key and
never an overwrite, and the row's pointer set is what defines a version. Reading verifies the digest
before parsing, so a truncated upload fails loudly instead of producing a task that looks fine and is
not.

`content/` in the repository keeps exactly one job: the curated seed that ships with the image. The
seeder imports it once, by content hash, and never overwrites a task that an admin created. A generated
task never enters the repository, which is why `hidden` can mean hidden — the repository is public.

The artifact set for one task:

```
task.json                     metadata, plan echo, subtasks, cast, provenance, object pointers
statement.en.md               public, may contain the story
statement.th.md
story.en.md                   the narrative layer, referenced by the statement
story.th.md
generator/                    writes input for one test group, seeded, deterministic
validator/                    rejects malformed input before it reaches a solution
brute/                        the slow but obviously correct reference
solutions/main.cpp            the intended algorithm
solutions/decoy.cpp           the algorithm the statement leans toward, when misdirection is on
solutions/subtask-<id>.cpp    one per subtask: proves that subtask is reachable without the full idea
hints/hint-1.md … hint-3.md   nudge, technique, pseudocode
```

`task.json` gains `subtasks`, `cast`, `misdirection` and `provenance`; `origin` keeps `kind: "ai"` and
records `model`, `prompt_version`, `template_version` and `seed`.

## Subtasks

A subtask is a real scoring unit, not a label.

- `constraints` — the value range that makes the simpler idea sufficient, per subtask.
- `test_group` — the tests that belong to it; the judge sums the groups it passed.
- `subtask-<id>.cpp` — a solution that **must pass its own group and fail at least one test outside
  it**. That single assertion is what makes the constraint table an achievable promise rather than a
  decoration.

A subtask whose proving solution passes everything means the subtask is not a subtask; it is the full
problem with a smaller name, and the gate rejects it.

## Misdirection

`misdirection.enabled` makes the statement read as if the intended approach is `lean_to`. It is a
deliberate property of the task, and it is only allowed when it is **measured**:

- `solutions/decoy.cpp` implements `lean_to` competently.
- The decoy must score **above zero and strictly below full** — above zero so the trap is a trap and
  not a wall, below full so the naive reading cannot complete the task.
- The subtask weights must be arranged so the decoy passes the subtasks it plausibly would, which is
  what makes the partial score honest rather than random.

`phrasing` lets the admin hand-write the misleading sentence instead of accepting the generated one. A
manual phrasing still has to satisfy the anti-echo and anti-leak gates below.

## Hints

Three fixed levels, generated with the task and cached with it:

1. **Nudge** — reframes the question without naming a technique.
2. **Technique** — names the class of idea, not the algorithm, unless the plan permits it.
3. **Pseudocode** — the shape of the solution without the code.

Unlocks are declared, not improvised: by request, or after a number of failed submissions. Each hint is
a file, so it is reviewable, diffable and gated.

## Cast

Named people in the statement, chosen to fit the story rather than sprinkled at random.

- `naming: "th-en-paired"` means every person has one canonical name and one localised form, so the
  Thai and English statements describe the same people.
- `people` and `role` bound the cast; a statement that invents an extra named character fails the gate,
  because an unbounded cast is how a generated story drifts from its plan.
- The cast is recorded in `task.json` so a later hint or solution can refer to the same people.

## Gates

| Gate | Fails when |
|---|---|
| `check:brief-leak` | any published artifact shares a verbatim span of eight words or more with the brief, the description, or the plan |
| `check:algo-leak` | `misdirection.enabled` and the statement names `lean_to` or the intended algorithm outside the allowed hint level |
| `check:hint-leak` | a hint contains a distinctive line of the reference solution, or names a hidden algorithm |
| `check:cast` | a named person appears in a statement without a cast entry, or the two locales disagree about who they are |
| `check:generator` | two runs with the same seed produce different bytes |
| `decoy-scores` | the decoy scores zero, or full |
| `subtask-solution` | a subtask solution passes tests outside its group, or fails its own |
| `check:exposure` | a `hidden` task is returned by the serving query, or a `shared` task has no published status |

Each ships with a falsification case, matching every other gate in this repository.

## Why this shape: the evidence

Each rule above is a known construct, not a preference. The names matter because they tell a future
reader what to search for when the rule looks wrong.

**Hidden by default is item exposure control.** In adaptive testing, a greedy selector always
administers the most informative item, so a small number of items reach most test-takers and the rest
of the pool is never used. Sympson and Hetter (1985) separate the probability an item is *selected*
from the probability it is *administered*, and cap the latter; uncontrolled selection measurably
degrades test security and wastes the bank (reviewed in Georgiadou et al., 2007). An admin-generated
task is the same problem in a different guise: it is new, uncalibrated and exactly the thing a serving
path would grab first. Defaulting to `hidden` is the cap.

**Not reused is the same argument at the level of the whole bank.** A task that has been seen by a
cohort stops measuring anything, because the answer circulates. The same decay is why code benchmarks
now ship contamination-free variants (LiveCodeBench, arXiv:2403.07974): once a task is in a corpus, its
value as evidence drops, and reuse is the mechanism.

**Admin generation without a cache is the distinction between item banking and on-the-fly assembly.**
Adaptive-testing practice separates a calibrated pool that is selected from, and a test assembled at
delivery time against constraints. A participant request is a selection with constraints; an admin
request is an assembly. Giving the assembly step a selection cache would collapse the two.

**The misdirection is productive failure.** Kapur (2008) and Kapur and Bielaczyc (2012) show that
students who first attempt a complex problem without scaffolding, and fail, outperform students given
direct instruction — provided a consolidation phase follows. The decoy is that generation phase: it
must be plausible enough to be attempted (score above zero) and insufficient enough that the canonical
idea is still required (score below full). Requiring the decoy to be *measured* is what keeps the trap
pedagogical rather than cruel.

**The hint ladder is the assistance dilemma.** Help that arrives too early removes the very struggle
that produces learning; help that never arrives is abandonment. Koedinger and Aleven's framing of the
assistance dilemma, and the Hint Factory line of work that predicts when a student needs help
(arXiv:2010.04124; survey in arXiv:1908.11566), are why the ladder is fixed at three levels with
declared unlock rules instead of a model improvising per request. A hint is also *checkable*: recent
work validates a hint by giving it to a weaker model and seeing whether the problem collapses
(arXiv:2310.03780). That is the shape `check:hint-leak` takes.

**Subtasks with proving solutions are scaffolding made honest.** Partial credit is only meaningful if
each tier is reachable with less than the full idea. A subtask solution that passes its own group and
fails outside it is the smallest evidence of exactly that, and it is why the constraint table cannot
drift from what the tests actually require.

**The brief/statement split is an instruction-following boundary.** A model asked to produce a
document from a description will reproduce the description; that is the default behaviour, not a bug in
any particular model. Separating a directive from an artifact, and checking for verbatim overlap
afterwards, is the only form of this rule that survives a model upgrade.

## Provenance and the cache key

The cache key in `docs/content-model.md` already covers `prompt_version`, `template_version`, `model_id`
and `model_params`. This document adds one field to it: the canonical plan. Two admins writing different
briefs that produce the same plan share a generation, which is correct — the brief shaped it, the plan
is what it is. A different plan is a different task.

Every generated task records the model, the versions, the seed and the admin, so a task can be
reproduced or withdrawn. The admin path records the same provenance but takes no key: it is outside the
cache by design, so the same brief twice produces two tasks, and the second is not free.

## Admin surface

One page, three panels:

1. **Plan** — topics, difficulty, limits, subtasks, misdirection, cast. Validated against the plan
   schema before anything is generated.
2. **Brief** — free text, clearly labelled as a directive with a note that it is not published. This is
   the field that produces the failure this document exists to prevent, so the labelling is part of the
   specification, not decoration.
3. **Preview** — the statement, the story, the hints and the score a decoy would get, rendered before
   anything is written to the bank.

Writing to the bank is the same two-phase shape as settings and schema: a plan previews the diff, an
apply creates the drafts, and nothing reaches students without verification.

## Failure modes this design accepts

- **A brief that describes the answer.** The anti-echo gate catches a copied span, not a paraphrase. A
  model that turns a directive into plot is working correctly; one that turns it into the statement is
  caught by the same gate, and a paraphrase that survives review is caught by a human reading the
  preview.
- **A decoy that is unfair.** Measured, not judged: the decoy's score must be strictly between zero and
  full, and the subtask split decides what "partial" means.
- **A hint ladder that gives the answer at level three.** Accepted, because level three is pseudocode
  and is unlocked deliberately; the gate forbids the solution's distinctive lines, not the idea.
