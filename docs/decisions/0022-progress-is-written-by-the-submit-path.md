# 0022 — Progress is written by the submit path

Status: accepted 2026-11-10 · decision by the operator

## 1. The defect

`deploy/schema/progress.sql` defines the table that records what a learner has touched, and
`bank::next_unseen_task` anti-joins it to decide what to offer next. Nothing in the service ever
writes it. The only writer in the tree is the gate that proves the query's plan
(`tools/check-unseen-task.sh`), which seeds the table itself.

Measured on a fresh plan and the shipped seed: the next-task query returns `p-easy` on two
consecutive runs. It cannot advance, because the set it excludes against is always empty.

A page that reads that table would therefore be correct and permanently useless, and a "next task"
would be the same task forever.

## 2. The decision

The submit path writes `progress`. One rule, in one place:

| Column | When it is set | When it moves |
|---|---|---|
| the row | a submission arrives for that account and task version | never duplicated — one row per pair |
| `attempted_at` | the first submission for that pair | never |
| `solved_at` | the first submission that is accepted | never |
| `status` | `attempted`, or `solved` once a submission is accepted | with `solved_at` |

The existing constraint that solved status and solved time agree applies in every state, including
an accepted first submission followed by a rejected second one.

## 3. What was rejected

- **Read `submissions` on the page instead.** No new write, and it works today. Rejected because it
  leaves the table, its unique index and the query built for it permanently dead, and because every
  page would then carry its own idea of what "attempted" means.
- **Let the gate keep seeding `progress`.** That is how the defect stayed invisible: a proof that
  seeds its own input proves the query and not the product.

## 4. Consequence worth stating

`progress` is per task **version**, not per task. A task edited to a new version is a new thing to
attempt, and the learner's earlier attempt stays attached to the version they actually solved.
