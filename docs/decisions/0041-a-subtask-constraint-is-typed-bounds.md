# 0041 — A subtask's constraint is typed bounds, and the sentence is rendered from them

Status: accepted 2026-11-18 · decision by the operator

## 1. The question

`0033` §2 makes a subtask **points plus a constraint**, and the constraint defines its group. The column
stores a sentence (`deploy/schema/content.sql:91`, `constraint_text`), with a comment explaining that the
values belong to the statement rather than to a second vocabulary.

`0040` then decided that one sampler produces every small input — including the cases **inside** a
subtask's group. A sampler cannot read a sentence, so the constraint must become something a program can
read.

## 2. The decision

| What | How it is stored |
|---|---|
| the subtask's constraint | a typed narrowing of the task's own bounds — the same fields, with "absent" meaning the full bound |
| the sentence a teacher and a student read | **rendered** from those fields, never typed independently |
| a narrowing that is empty, or wider than the task's bounds | refused at write, as `0033` already requires for "wider" |

One set of numbers, two readers: the sampler samples inside it, and the statement's scoring section prints
it (`0033` §4 already says the scoring text is generated from the rows).

## 3. Consequences

- A schema change: the text column becomes the typed fields, the deployment plan and the bindings are
  re-rendered, and `check:schema` proves the committed plan is the rendered one.
- No data migration is owed for seeded content: no shipped task carries a subtask row today (verified:
  `grep subtasks content/tasks/*/task.json` finds nothing).
- The differential check gains a measurable rule: the subtask's own solution must pass every case the
  sampler draws **inside** the narrowing, and fail at least one drawn **outside** it but inside the full
  bounds. That is the assertion `0033` calls "what makes the constraint table an achievable promise".
- The partition trigger (`task_subtasks_are_a_partition`) is untouched: it asserts facts about the set of
  rows, not about one column's shape.
- `0015` §1 row 5's "the weakest subtask's solution passes its own group and fails at least one test
  outside it" becomes checkable without the test step, which is why `0040`'s sampler is what makes this
  record possible.

## 4. What was rejected

- **A parser over the teacher's sentence.** It must understand every sentence a teacher writes, and a
  sentence it cannot read becomes a refusal on content that is otherwise valid.
- **Both the sentence and a separate range.** Two facts that can disagree, and no rule that says which one
  the sampler and the student are reading.
