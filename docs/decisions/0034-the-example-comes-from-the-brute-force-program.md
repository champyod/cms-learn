# 0034 — The statement takes its example from the brute-force program

Status: accepted 2026-11-17 · decision by the operator

## 1. The question

The statement's fixed sections (`0031` §2) include an example. That section needs an input and its
answer, and only a program can print an answer a gate can check.

Two accepted records disagree about where it comes from:

- `0015` §6 — the statement reaches the administrator **before the tests exist**, and the state that
  creates is "a draft whose statement exists and whose tests do not".
- `0031` §2 — "examples | **derived from the generator**, never written by the model".

The generator is the test step, and the test step runs after the statement. So at the moment the
statement is written, the producer `0031` names does not exist yet: the statement would either wait
for the tests or ship a hole where a fixed section goes.

## 2. What already exists before the statement is written

`generator/src/specification.cpp:56` samples the smallest case the declared bounds allow, from
`min_n` and `value_min`. `5f7a60c` stores the brute-force program that answers the specification,
compiled, with its digest, read back through the verified read. One machine-derived input and its
answer are therefore available one step earlier than the generator.

## 3. The decision

| Part of the example | Who produces it |
|---|---|
| the input | the smallest-case sampler over the declared bounds |
| the answer | the stored brute-force program, run on that input |
| the prose around it | the statement writer, and never a number |
| how many | one pair, shown in both locales |
| the graded cases | the test step, unchanged, and they are not this example |

The statement is complete when the administrator first reads it. The draft state `0015` §6 names
becomes "tests pending", and nothing else is pending.

## 4. What it amends in 0031

One row of §2's artefact table, and no other. **"never written by the model" stands**: a value no gate
can compute is still the defect that row exists to prevent.

## 5. What was rejected

- **Filled in when the tests are made.** The first copy the administrator reviews has a hole where a
  fixed section goes, so the review is repeated when the tests land, and the hole invites exactly the
  placeholder invariant 6 forbids.
- **Make the tests before the statement.** It keeps both records intact and moves the administrator's
  first look behind the whole test step, which is the opposite of the ordering `0015` §6 chose.

## 6. Consequences

- The example is not necessarily one of the graded cases. That is the cost of two producers, and it is
  visible: a student can submit the example and the judge has no such case among the graded ones.
- The sampler gains a second caller, so it keeps one job — "the smallest case the bounds allow" — and
  the example and the oracle check both read it. It is never copied into the statement step.
- Both locales show the same input and the same answer; only the prose differs. A locale showing a
  different pair would be a second example under one name.
- `check:brief-leak` and `check:algo-leak` now guard a file whose example is machine-derived. Neither
  gate loses a case to this: the example was never the leaky part, and the prose still is.
