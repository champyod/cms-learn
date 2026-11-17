# 0036 — The request is stored on the job row, and the writer is told the trap, not its name

Status: accepted 2026-11-17 · decision by the operator

## 1. The defect the report found

Three written sources promised a stored request:

- `0014` §2 lists `brief` ("free text, never published") and `description` ("one line for the admin") among
  the form's fourteen controls.
- `docs/task-generation.md` says the brief "survives in the generation audit row".
- `deploy/schema/generation.sql:210` says the worker's role "holds the record and reads the brief".

The record has no such place:

- `generation_jobs` holds an id, a task id and version, a state, attempts, the claim columns and timestamps.
  There is no request column (`generation.sql:7-30`).
- the only task type declares six fields, and neither the brief nor the description is among them
  (`content/task_types.json:9-14`).
- `0030` §3 rejected storing the prompt, so the request is not recoverable from an artefact either.

`0035` §2 then assumed the statement step could write the brief to a temporary directory. It cannot: the
step never receives it, and `0015` §3 forbids it receiving it.

## 2. The decision

| What | Where it lives now |
|---|---|
| the teacher's free-text request | a column on the job row |
| the admin's one-line description | the same row, in its own column |
| the chosen plan | where it already is, `generation_candidates` |
| who may read them | the generator role only, which already holds the record |
| the writer's input | step 3's input, output and constraints — plus the trap's **shape** |

Both texts are written when the job is created, before any model call, and are readable only by the role
that runs the steps. The sentence at `generation.sql:210` becomes true instead of aspirational.

## 3. What the writer is told

`0015` contradicted itself: §3 gives the writer input, output and constraints only; §5 says the statement
is written to lean toward `lean_to`. The decision resolves it:

- the writer is told **the shape of the trap** — what a reader is meant to believe — never `lean_to`'s name
  and never the intended algorithm;
- so `0031` §2's "never names the intended algorithm" holds **unconditionally**: a statement naming a
  technique is a leak whether or not misdirection is on;
- the plan carries the one sentence that describes the trap without naming it. A plan that cannot express
  the trap without the name is refused by the step, never guessed.

## 4. Consequences

- The absence guarantee narrows to what it can promise: the writer never receives the request. The check
  compares the artefacts it writes against a request it never saw.
- The check reads the request from the row, not from an in-process value. A job row with an empty request is
  refused by name when the check runs, rather than compared against nothing.
- The description becomes a checked input, so it can no longer be dropped silently.
- Storing the request is a schema change, so the step that lands it re-renders the deployment plan and
  passes `check:schema`.

## 5. What was rejected

- **Handing the request to the worker and storing nothing.** It is the smaller change and it makes the
  audit row a claim the database cannot support: after the run, nobody can say what was asked.
- **Telling the writer `lean_to` by name.** The name would sit in a prompt that is neither stored nor
  gated, leaving only the check between it and a student.
- **No leaning at all.** It removes the trap from every statement, which `0015` §5 chose deliberately.
