# 0033 — A subtask is points and a constraint; the limits stay on the task

Status: accepted 2026-11-16 · decision by the operator, after checking the olympiad rules

## 1. The correction this record makes

I proposed a table where a subtask carries **its own time and memory limits**. The operator asked me to
check what the olympiads actually publish, and it is the other way round:

- *"Some programming tasks are divided into subtasks. Points are awarded per subtask and added. Each subtask
  involves a part of the output data."* — IOI '95 contest rules
- *"Each task is divided in[to subtasks]"* — eJOI 2025 rules
- a task page states the limits **once, for the task**: *"Time Limit: 3 s · Memory Limit: 2048 MB · Total
  points: 100"*

So **per-subtask limits are the exception**, used by a few tasks that want a slower partial solution to fit.
The model is: a subtask is a **group of tests**, worth **points**, defined by a **constraint**.

## 2. The decision

| A subtask is | Where it lives |
|---|---|
| its position in the task | a row |
| its points | a row |
| the constraint that defines its group (`n ≤ 2000`) | a row |
| time and memory | **on the task**, stated once — never per subtask |

A task that genuinely needs a different limit for one group is not supported by a column that would be null
almost always and wrong whenever the two disagreed. It is out of scope until a real task asks for it, and it
would then be its own record with its own rule for which limit wins.

## 3. What was rejected

- **A per-subtask limit pair**, which I proposed first. It puts a limit in two places, and every reader then
  has to ask which one applies — the defect this project has spent a week removing.
- **Subtasks inside the task's field values as a collection.** Nothing new in the schema, and nothing a
  constraint can inspect: a scoring table a statement promises would live in a JSON blob.

## 4. Consequences

- `score_mode` already offers `subtasks`; a task that selects it now needs rows to score against.
- The certificate that decides a task is verified must know how many subtasks it has and what each is worth,
  because "the reference passes every case" is not the same as "every subtask's points are earned by a
  program that earns them".
- The statement's scoring section is generated from these rows, so what a learner reads and what the judge
  awards cannot drift.
