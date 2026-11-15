# 0030 — One artefact row per model call

Status: accepted 2026-11-15 · decision by the operator

## 1. The gap the merge signed around

Steps 1 and 2 call a model and record the *answer* — the candidate and the decision — but nothing records
**which call produced it**. `generation_artefacts` already carries `model_id`, `prompt_version`, `seed` and
`call_index`; `CandidatePlan.call` is dropped at `generator/src/candidates.cpp:23-34`. So a candidate cannot
be traced to the request that made it.

## 2. The decision

Every model call writes **one `generation_artefacts` row**, whatever step made it. The row names the step,
the kind, the model, the prompt version, the seed and the call index, and points at the answer.

| Property | Why it follows |
|---|---|
| one pattern for all seven steps | the columns already exist and mean the same thing at every step |
| a candidate is traceable | the candidate row names the artefact that produced it |
| a re-run is comparable | two calls differ by seed and call index, not by a hand-written note |

## 3. What was rejected

- **Provenance columns on `generation_candidates`.** Adjacent to the decision and one table to read, but
  steps 3 to 7 would each add their own columns and the schema would grow a pattern per step.
- **An artefact for the prompt and one for the completion.** The strongest audit — a run becomes exactly
  replayable — and it doubles the storage for something almost never read. Recorded here as the natural
  upgrade if a dispute ever needs the exact prompt.

## 4. Consequences

- A step that calls the model without writing its call row is incomplete, and its test must fail.
- The row is written in the same transaction as the answer it produced, so an answer cannot exist without
  the call that made it.
- `generation_candidates` gains a reference to the call, and the brief's digest is not needed: the call row
  is the record.
