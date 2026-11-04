# 0011 — The learner's token balance

Status: accepted 2026-11-04 · decision by the operator

## 1. What a token buys

| Action | Cost |
|---|---|
| Ask the system to create a new task | 10 |
| Reveal one hint section | 1 |

A task normally carries several hint sections, so working through a task's hints costs less than
creating a task, and reading every hint of one task costs about the same as creating it.

## 2. How the balance moves

- A balance refills 10 tokens every 10 minutes.
- It holds at most 20 tokens, which is two creations back to back.
- The balance is shown in the interface as a currency figure, not as a rule sentence, so a learner
  reads it the way they read a price.

## 3. Why a balance rather than a limit on creations

The same budget pays for both spending paths, so a learner chooses between "another task" and "a hint
for this task" instead of hitting two unrelated limits. It also keeps the queue bounded without a
per-day counter that resets at midnight and rewards waiting until after midnight.

## 4. Consequences accepted

- A hint is now a thing the system owes the learner, so hint sections must exist per task and must be
  paid for individually. Whether they are written at task-creation time or on demand is still open.
- The balance is a row per learner with a timestamp; refill is computed from the timestamp, not from a
  scheduled job, so a stopped worker does not silently stop the economy.
