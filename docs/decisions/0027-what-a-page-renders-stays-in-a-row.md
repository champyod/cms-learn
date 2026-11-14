# 0027 — What a page renders stays in a row; what a tool reads goes to the store

Status: accepted 2026-11-12 · decision by the operator · corrects an assumption in the pipeline design

## 1. The contradiction this settles

`0023` says a task's bytes live in object storage with the row holding the key. The schema written before it
stores the statement as text in `task_statements` and the reference solution as text in `tasks.solution` —
the shape `0023` §4 explicitly rejected as "a row holding the bytes directly".

The pipeline design then assumed the statement would become a file field holding an artefact id, which is
one of three homes it currently has and which the operator rejected.

## 2. The decision

Split by **what reads it**:

| What | Home | Why |
|---|---|---|
| statement text per language | a row | it is rendered on a task page; a row read is one read |
| the reference solution, the proof, the test set, the generator and checker sources | the object store, content-addressed, digest in the row | a tool consumes them, they grow, and a page never renders them |
| plans, the chosen plan with its reasons, limits, provenance, evidence | rows | they are small, queried, and never rendered |

So `tasks.solution` and `task_statements` stay as they are, and the artefacts that the pipeline produces go
through the one guarded client.

## 3. What this changes in `0023`

`0023` §2 said "the bytes live in an object store" without distinguishing what a page renders. This record
narrows it: **a page's text is a row; an artefact is an object.** Everything `0023` says about the store —
the required credential, the missing object refused by name, the key in the row — applies to the artefacts.

## 4. What was rejected

- **Everything in the store**, as the pipeline design assumed: every task-page view would fetch the statement
  over the network, and the statement is the one thing every learner reads.
- **Everything in rows**, as the schema does today: the test set grows without bound in the database, and the
  store client built for it becomes decoration.

## 5. Consequence for the pipeline

Step 4 (the statement writer) writes a **row**. Step 5 (the solution writer) and step 6 (the test case
writer) write **objects**. A `file`-kind field holds a `generation_artefacts.id`, never text — the database
enforces it with a trigger, the form refuses a key by name, and the reader resolves the id to the object's
key and digest before serving anything.
