# 0044 — The test count is the operator's to set, and the pipeline's to decide when it is unset

Status: accepted 2026-11-18 · decision by the operator

## 1. The question

The records fix the *order* of a task's tests ("low-resource cases first", `0015` §1 row 6) and the *proof*
each one carries (the checker accepts the reference and rejects a planted wrong answer). They never say how
many cases a task carries, and that number is what a student actually faces.

## 2. The decision

| What | The rule |
|---|---|
| the count, per subtask | a value the operator **may** set |
| the count, when unset | the pipeline decides, and **records the number with the reason it chose it** |
| the order | lowest resource first, unchanged (`0015` row 6) |
| where the number lives | with the task, as data — never a constant in code |

Two readers follow: the operator, who sets it when he has a trade-off in mind, and a later reader, who can
see what the student faced and why.

## 3. Why

A fixed constant makes "this task has twelve cases" a property of the program rather than of the task, and a
rule that knows the count cannot know the shape: a graph task and an arithmetic task need different numbers.
Customizable data with a recorded choice keeps the operator's decision first and the pipeline's decision
visible.

Absence here is **not** a silent default. The pipeline fills it deliberately and says so in the record; an
unrecorded number would be a decision taken alone.

## 4. What this record does not settle

**A maximum.** Nothing here bounds how many cases the pipeline may write. The test writer's contract must
ask for that bound before it runs, or a model can write a number nobody can judge inside the task's own time
limit.

## 5. What was rejected

- **A fixed count in code.** It cannot know the shape of the task.
- **The model deciding silently.** An unrecorded number is a decision nobody can review.
