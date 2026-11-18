# 0050 — A task whose repairs run out waits for a person, with the last refusal attached

Status: accepted 2026-11-18 · decision by the operator

## 1. The state that already exists, and the one thing it left open

`deploy/schema/generation.sql:47-72` already fixes the shape: a job moves `queued → running →
{awaiting-evidence, done, failed}`, and **`done` and `failed` are terminal** — "a finished or failed job is
not claimable", and nothing reopens it.

So the loop's end is a row that can no longer move. What the record never said is what a **person** sees at
that moment, and that is what this record decides.

## 2. The decision

| What | The rule |
|---|---|
| when the repairs run out | the job stops in `failed` and **keeps its last refusal** |
| where a person finds it | one list of failed jobs: the task, the last refusal, and how many attempts were made |
| what a person may do | send it back for repair — which is a **new task version**, since a failed job cannot be reopened — or abandon it **with a reason** |
| what must never happen | the reason disappearing, or the pipeline retrying without a bound |

## 3. Why

A quiet deletion removes the one artefact the next attempt needs: what failed and why. An unbounded loop
spends model calls on a task that may be impossible, and nothing on the operator's screen would show it
burning.

A list is the smallest surface that makes the failure visible and the decision explicit. It also matches the
state machine already in force: retrying is a new version, never a reopened row — "a re-run is a new task
version, which is what the version is for" (`generation.sql`).

## 4. Consequences

- The administrator surface owes one list: failed jobs, newest first, each carrying its refusal. That is a
  screen of its own, and it is where this record becomes visible to a person.
- The last refusal is evidence (`0049`), so the list is also a record of what the pipeline could not fix.
- Abandoning a task is a **chosen** state with a reason, never a silence. A task that nobody decided about
  stays in the list.
- Because `failed` is terminal, "repair it" is always "make a new version" — the loop can never quietly
  resume work a person believed was finished.

## 5. What was rejected

- **Deleting the task with everything it produced.** It removes the reason it failed, which is the only thing
  the next attempt can learn from.
- **Repairing until it passes.** One impossible task can consume the pipeline forever, and no screen would
  show it happening.
