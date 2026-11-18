# 0046 — The verified state is written by a certifier that re-ran the checks

Status: accepted 2026-11-18 · decision by the operator

## 1. The gap this record closes

The rule is old and written in two places: a task reaches a learner only on **machine evidence**
(`docs/content-model.md:60-66`, and the repository's stated invariant that `published` requires it).

What the tree actually does today:

| Where | What it shows |
|---|---|
| `judge/src/promote.cpp:94-95` | it searches the task's **JSON text** for `"status": "verified"`, then re-runs the checks |
| the only writes of `'verified'` in the repository | test fixtures (`api/tests/*_e2e.sh`) |
| anything in production that writes it | **nothing** |

So "verified" is a **string in a file** that someone typed. The judge then re-runs its own checks before
publishing — but the state the file claims was never produced by running anything.

## 2. The decision

| What | The rule |
|---|---|
| who writes `verified` | **one certifier**, and nothing else |
| on what basis | checks **it re-ran itself**, in that run |
| where the result lives | the database row, carrying the evidence it ran |
| the file's claim | retired — a task's file stops saying `verified` |
| publication | requires the row, never the string |

## 3. Why

A string in a file is a claim by whoever wrote the file, and this project's own rule says a task's state is
a machine fact. A row written after a re-run is a claim the system made and can defend with the run behind
it. Keeping both would let the file say verified while the last run said otherwise, and a reader would have
to know which one wins.

## 4. Consequences

- Certifying is a **full re-run**: reference-pass, brute-force differential, and limits-measured
  (`docs/content-model.md:61-66`). Its cost is the cost of the evidence, and it is recorded rather than
  assumed away.
- The row records the **digests** it certified. A task whose artefacts changed after the run is no longer
  verified by that row, so evidence cannot outlive the thing it proved.
- `promote.cpp` reads the row instead of the string; removing the string is a change to the task file
  format and its fixtures.
- The worker's role gains exactly one privilege — writing the verification row — and nothing that
  publishes.

## 5. What was rejected

- **The file keeps the claim and a run records the result.** Two sources that can disagree about one task.
- **The operator marks it verified.** A human claim where the rule asks for machine evidence, and the task
  that most needs the evidence is the one nobody has time to inspect.
