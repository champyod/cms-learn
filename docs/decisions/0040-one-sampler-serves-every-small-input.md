# 0040 — One sampler serves the example, the differential check and the tests

Status: accepted 2026-11-18 · decision by the operator

## 1. The question

Proving a solution by brute force needs many small inputs. Today one sampler builds **the single
smallest case** the declared bounds allow (`generator/src/specification.cpp:56`), and it is used by the
example (`0034`) and by the oracle check.

The records fix the method and the order:

- `docs/content-model.md:61-66` — a task is `verified` only when "on small random inputs the reference
  agrees with a brute force".
- `0015` §1, row 5 — "the reference agrees with brute force on the small cases".
- `0015:20` — and it happens **before** the reference's answers generate the full test set, "because a
  reference that is wrong and used as an oracle produces a test set that is wrong in the same way".

What no record fixed is **who produces those inputs**.

## 2. The decision

One sampler, in one place, serving three callers:

| Caller | What it asks for |
|---|---|
| the example (step 4) | the smallest case, as today |
| the differential check (step 5) | several small inputs from one seed |
| the tests (step 6) | the cases each of its groups needs |

It reads the declared input shape and the declared bounds. The seed is an argument, never the clock, so
the same seed produces the same bytes. A shape it cannot sample is refused by name, exactly as today.

## 3. Why not a sampler inside the solution step

It would be a second program doing the same job, and the test step would then write a third. The three
could disagree about what a valid small input is, and the example, the differential check and the tests
would no longer be provably drawn from the same distribution. The reuse rule and the single-source rule
both point at one sampler.

## 4. Why not wait for the test generator

`0015:20` makes the order a rule, not a preference: the reference is certified **before** its answers
generate the tests. Waiting reverses it and hides the very defect the check exists to find — a wrong
reference producing a test set wrong in the same way.

## 5. Consequences

- The sampler owns the vocabulary of input shapes. When the plan declares a shape it cannot sample, the
  step refuses rather than inventing a test the specification never named.
- Step 6 builds on the sampler and never writes its own; a second sampler is a defect, not a shortcut.
- The example keeps its guarantee from `0034`: the sampler's output, run through the stored brute-force
  program.
- A subtask's group is its constraint (`0033`), so step 5 will ask the sampler for inputs *inside* a
  subtask's constraint and *outside* it. That requires a constraint a program can read, which is the
  next decision this record's consequence opens.
