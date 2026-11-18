# 0048 — The reference solution may use up to half the task's limit

Status: accepted 2026-11-18 · decision by the operator

## 1. The open word

`docs/content-model.md:66-67` makes a task verified only when "the reference runs inside the declared
`time_ms`/`memory_mb` on the slowest test **with margin**, so a correct-but-slow student solution is not
failed by a wrong limit". It never says how much margin.

The limit itself is already derived at step 3 from the plan's complexity
(`OPERATIONS_PER_MILLISECOND = 100'000`, `generator/include/cms/generate/specification.hpp:76`) and is
"never loosened by hand" (`0015` row 3). So the margin is the last unwritten promise the project makes to a
student.

## 2. The decision

| What | The rule |
|---|---|
| the slowest case's measured time | at most **half** the declared time limit |
| the measured memory | at most **half** the declared memory limit |
| a reference above that | the task is **refused at certification**, and the message names the measured value and the limit it broke |
| a student | a correct solution up to **twice** the reference's running time passes |

## 3. Why

The margin is a promise to students, not a performance target. A correct answer whose constant is slower
than the reference's must still pass, and a wrong **complexity** must still fail — the statement was written
to reject it. Half the limit keeps both true: it leaves a factor of two for a legitimate slower
implementation, and it does not leave the factor of three or more that lets a solution one complexity class
worse slip through when its constant happens to be small.

A third was rejected because every limit would grow and a slow task would get an even slower limit; no
margin was rejected because it fails a correct solution for a constant factor.

If a task's own measurements ever justify a different number, that is a new record with those measurements
behind it — not a quiet change to this one.

## 4. Consequences

- Certification (`0046`) refuses a task whose reference is over half the limit on its slowest case, naming
  the measured time, the limit and the case.
- A task whose intended solution is genuinely slow therefore raises its limit at step 3, where the limit is
  derived — or is refused there. It cannot be "fixed" by certifying anyway.
- Memory follows the same shape, so a memory-heavy reference is refused the same way rather than passing
  because only time was measured.
- A reference that fits at half but not at a third is exactly the case this record decides: without it, the
  same task would be accepted or refused by whoever happened to run the gate.

## 5. What was rejected

- **A third of the limit.** Every limit grows, and slow tasks get slower limits for no gain in safety.
- **No margin.** A correct solution with a slower constant is failed, which teaches the operator to distrust
  a gate that is right.
