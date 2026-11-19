# 0049 — A refusal carries every check's result, so the repair fixes all of it in one pass

Status: accepted 2026-11-18 · decision by the operator

## 1. Who is actually in the loop

The first draft of this question asked what "you" read and fixed. That was wrong, and the operator caught
it: in this project's workflow the loop has **no person in it**.

| Actor | What it is | Its job |
|---|---|---|
| producer | the pipeline | writes the statement, the reference answer and the tests |
| certifier | a program (`0046`) | re-runs the checks, refuses or writes the verified row |
| repair | the **same pipeline**, running again | reads the refusal and produces a corrected version |

`0015` §2 already allows this: successive attempts "whose acceptance is decided by the verifier, never by
agreement". A person stands outside the loop and sees a task when automation gives up, or when they authored
one deliberately.

## 2. The decision

| What | The rule |
|---|---|
| the certifier | runs **all** the checks before it answers, even after one has failed |
| the refusal | carries every check, its verdict, and **the measured value behind it** |
| the repair | receives all of them and can fix everything in one pass |
| a partial report | never — a refusal that names only one failure is a defect |

## 3. Why

A repair that learns one failure at a time repeats the whole pipeline once per problem, and every repetition
re-runs model calls and compiles programs. The checks themselves are cheap once the artefacts exist, so
running all of them costs almost nothing and saves a full loop iteration per finding.

It also removes a failure mode. A repair that fixes the reported failure and reintroduces another has more
chances to do so the more passes it takes; one pass over a complete list is the smaller risk.

## 4. Consequences

- The certifier collects, then answers. It never stops at the first failure.
- Each entry carries the measurement, not just a verdict: "3.1 s on case 7 against a 2.0 s limit" is
  actionable, "the limits check failed" is not.
- The row `0046` introduces records the whole set, so a later reader sees exactly what was reported.
- A repair that cannot fix everything at once still makes one pass, and the next refusal reports what is
  left. The loop terminates on the verifier's answer, never on a person's opinion.

## 5. What was rejected

- **The first failure only.** The loop runs once per problem, and each run costs model calls and compiles.
- **A stored file the repair may skip.** A repair that does not read it repeats the same failure, and nobody
  sees that it did.

## 6. Amendment, 2026-11-19 — one sentence of §3 was wrong

§3 says "the checks themselves are cheap once the artefacts exist, so running all of them costs almost
nothing". Measurement contradicts it: `run_program` (`generator/src/oracle.cpp:99-131`) writes the program
into a fresh scratch directory, **compiles it**, runs one case, and deletes the directory — on every call. A
step-6 scoring pass over four solutions and six cases is dozens of compiler invocations, not an almost-free
scan.

What survives is the argument that matters: one pass over a **complete** list still saves a whole repair
iteration per finding, and an iteration costs a model call plus a full recompile of everything. What does not
survive is the claim that collecting every check is nearly free — it is not, and a reader planning the
certifier should budget for it accordingly.

The rest of this record stands unchanged.
