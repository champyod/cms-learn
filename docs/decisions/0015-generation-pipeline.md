# 0015 — The generation pipeline: seven steps, sharded, statement first

Status: accepted 2026-11-04 · decision by the operator

## 1. Seven steps

The pipeline the operator described, in order. Each step receives the previous step's output and
nothing else; each gate is machine-checkable.

| # | Step | Receives | Produces | Gate |
|---|---|---|---|---|
| 1 | Planner | the brief (the fourteen rows of `0014-task-authoring-form.md`) | **at least four** candidate plans, each naming its approach, its intended complexity and its fit to the declared bounds | four is a floor |
| 2 | Judge | the candidates | one chosen plan, the rejected ones kept with their reasons | the choice and its reason are recorded |
| 3 | Proof agent | the chosen plan | a formal specification, a machine proof, and the limits for the whole task and for each subtask | the verifier accepts; a limit is derived from complexity and bounds, never loosened by hand |
| 4 | Statement writer | **input, output and constraints only — no solution, no technique** | statement in TH and EN, and the constraints section | anti-`brief-leak` and anti-`algo-leak`, plus an input that could not leak because it never held the approach |
| 5 | Solution writer | the theory (specification and proof) | the reference solution, and each subtask's solution | the reference agrees with brute force on the small cases; the weakest subtask's solution passes its own group and fails at least one test outside it |
| 6 | Test case writer | the per-subtask limits from step 3 | generator code, the checker, low-resource cases first | the checker accepts the reference and rejects a planted wrong answer; the generator is deterministic under the recorded seed; the sandbox produces every one of the nine verdict kinds |
| 7 | Certifier | all evidence | provenance, digest, the version | every earlier gate is re-run, and nothing reaches a learner before it does |

The reference is certified by brute force on low-resource cases *before* its answers are used to
generate the full test set — the ordering matters, because a reference that is wrong and used as an
oracle produces a test set that is wrong in the same way.

This describes the admin path, which always generates and never reads a cache. The participant path
puts the cache lookup of `docs/content-model.md` in front of the same seven steps and is otherwise
unchanged.

## Interpretation added 2026-11-17 — what "a machine proof" means, and where the limits live

Step 3 asks for "a formal specification, a machine proof, and the limits for the whole task and for each
subtask". Two of those need reading against what was decided later.

**The machine proof is a differential check, not a formal proof.** *"the verifier accepts"* at step 3 and
*"the reference agrees with brute force on the small cases"* at step 5 name the same evidence: a brute-force
oracle the pipeline also writes, run against the reference on inputs small enough to enumerate. That is what
makes a claim about correctness machine-checkable here — a proof assistant is a different project, and a prose
argument is not machine evidence at all. Feature `F-012` is this check.

**The limits are the task's, and a subtask has a constraint instead.** `docs/decisions/0033` records why: the
olympiad statements state one time and one memory limit per task, and per-subtask limits are the rare
exception. So step 3 derives **one** limit pair from the complexity and the bounds, and each subtask carries
the constraint that defines its group.
## Interpretation added 2026-11-14 — distinct approaches, not four copies

Step 1 says "at least four candidate plans". It does not say the approaches must differ, and a reading that
added that rule silently would be this repository's own defect class. It is recorded here instead.

**Decision:** the four candidates must name distinct approaches.

**Why:** the floor exists so step 2 has alternatives to compare. Four candidates naming one approach give the
judge nothing to choose and make the rejection record meaningless, which is the outcome the floor exists to
prevent. A step that cannot produce four distinct approaches refuses before it calls the model, so the cost
is a named refusal rather than a padded list.

## 2. Parallelism is sharding, never redundancy

Parallel means one job cut into independent parts so that no call is a bulk call. It never means the
same job twice with a vote:

- A vote over a machine fact can report "verified" when the verifier said no.
- A vote over a judgement spends tokens to produce an average nobody can defend.

So the proof step may run many agents, but they are either parts of one proof or successive attempts
whose acceptance is decided by the verifier, never by agreement. Consequence: quality rests on the
gates, which is where this project already puts it — `published` requires machine evidence
(`AGENTS.md` invariant 3).

## 3. The statement writer is not told the answer

The repository already detects leaks after the fact: `check:brief-leak`, `check:algo-leak` and
`check:hint-leak` (`docs/task-generation.md:185-187`). Those stay. What this record adds is the input
side: step 4's input is rebuilt key by key from `io` and `constraints`, never filtered down from the
plan. A field that was never sent cannot leak, and the strongest guarantee here is an absence rather
than a scan.

## 4. Limits belong to step 3

Restating `0014-task-authoring-form.md`: the proof agent authors the pair for the task and for each
subtask; a pair the teacher typed wins, and the run is refused when no correct solution can fit inside
it. Step 6 then certifies by running the reference inside the limit. Two authors, one rule, and the
refusal is the useful signal.

## 5. The decoy is not a program

`lean_to` names an approach in the plan; the statement is written to lean toward it. The evidence that
the trap is fair is the **weakest subtask's solution**, which by construction passes its own group and
fails at least one test outside it — a score strictly between zero and full.

`solutions/decoy.cpp` and `check:decoy-scores` are therefore replaced by one coherence check: the
approach `lean_to` names must be the approach the weakest subtask solves. The two artifacts were the
same artifact; keeping both meant two programs, two gates, and a chance for them to describe different
traps.

## 6. The statement arrives first, and the state that makes that honest

The statement and the task's outline reach the administrator first, where the outline is the summary an
admin reads before the tests exist: the chosen approach, the proof's conclusion, the limits and the
subtask intents. The outline and that inner information are never rendered to a learner, only to an
admin.

Test generation then runs after that, asynchronously. This creates a state the vocabulary must name: a
draft whose statement exists and whose tests do not. It remains `draft`, because `published` requires
machine evidence, and the admin page labels it "tests pending" rather than leaving a reviewer to guess
whether a missing test set is a fault or a queue.

One reading must be refused: "the end user does not wait" cannot mean a learner sees this task before
step 7. Invariant 3 forbids serving an unverified task, and invariant 6 forbids a placeholder. It means
generation is triggered by a gap in the bank and the learner is served *another* task meanwhile, so no
learner ever waits on a pipeline.

## 7. What this supersedes

- The six stages in `methodology.typ:168-184` describe a different order and no planner or judge step.
  They are replaced by these seven. The document now says seven steps (`cms-learn-doc` `5250feb`),
  promises up to six brief fields with two required (`5e45889`), and states the generation queue
  (`3f484a8`).
- `docs/task-generation.md` loses `solutions/decoy.cpp` (`:125`), the decoy score gate (`:190`) and
  the `misdirection` example that assumes it, and gains the coherence check of §5.
- The generation input loses `track` and `stage` on the admin path (`0014`), which that document still
  shows.

## Consequences

- The weakest subtask now carries two jobs: partial credit and the trap. The subtask table must
  therefore be ordered by assumption strength, and the gate checks that ordering rather than assuming
  it.
- Sharding the proof step means several calls, so provenance must record *which* call produced the
  accepted proof — otherwise the record names a step and cannot say what it did.
- Step 2 is a single judge, not a panel. A bad choice is caught only by the gates downstream, not by
  agreement, which is the cost of §2 and is accepted.
- Rejected: voting anywhere; a statement writer that sees the plan and is filtered afterwards; a
  dedicated decoy program; publishing a task whose tests are still generating.
