# 0061 — A task carries a validator, and the certifier proves it bites

**Status:** accepted · **Date:** 2026-11-24 · **Decides:** how a generated input is proven admissible

## Context

Step 6 writes two programs per task: a seeded generator and a checker. The input those programs
produce was validated by one hand-written C++ function,
`validate_generated` (`generator/src/test_plan.cpp:113`), against `TaskBounds` — four integers,
`min_n`, `max_n`, `value_min`, `value_max` (`core/include/cms/subtask.hpp:10`).

That function understands exactly one input layout: a count, then that many integers, each inside a
value range, with nothing after them. Every other shape — a graph given as `n m` and `m` edges, a
string, a sequence of lines — is stored with no shape check at all, and the pipeline cannot be taught
a new one without a code change to the generator.

Two things follow. A generated `.in` that is inside its numeric bounds but wrong in shape passes
today. And nothing outside the pipeline can re-validate a stored input: there is no artifact to run.

## The decision

A generated task carries a **third program**, a testlib validator, with the role `validator` in
`generation_programs`. It is generated from the task's own specification, compiled the way the
generator and the checker are compiled, and it becomes the **only** definition of an admissible input
for that task.

The certifier proves it bites, in both directions:

- every case the pipeline generated must be **accepted** by it, so a validator that refuses everything
  cannot pass;
- a set of mutations must each be **refused by name** — a value outside the declared range, one token
  short of the declared count, one extra token after it, and an empty input.

A refusal is a verdict the certificate records with the mutation that produced it, so a reader sees
which rule was proven rather than that some rule was.

## What this replaces

`validate_generated` stops being the authority. It stays only as the fast in-process guard for the
integer-sequence layout, so a broken generator is caught before a compile. The emitted validator is
what the certificate names, and what an operator or a second deployment can run against any stored
`.in` without trusting this pipeline.

## Rejected

**Extend the C++ validator with a shape vocabulary.** It keeps one implementation and no extra
program, but every new task shape becomes a code change in the pipeline, and the vocabulary would
have to describe graphs, trees, strings and mixed layouts — a small programming language, written and
tested by hand, in the component that is supposed to decide nothing about task content.

**Keep the one-shape validator.** Cheapest, and honest only if every task is a list of integers. With
graph and string tasks in the model, it stores inputs nobody has checked.

## Consequences

- `generation_programs.role` gains `validator`, and the certificate names it beside the generator and
  the checker.
- Step 6 gains a validator prompt; step 7 gains the acceptance run and the mutation set.
- A task whose validator cannot be compiled, or accepts nothing, or refuses a generated case, fails
  the job with the step and the reason — the same refusal shape the other steps use.
- The judge may run the validator before a submission; that is a separate decision and is not taken
  here.
