# 0053 — Where the built code and the records disagree, the code changes

Status: accepted 2026-11-19 · decision by the operator

## 1. The question

Two sessions built the same steps of the generator. One had these records; the other did not. Three places
disagree, and each was measured rather than reported:

| Record | The built code |
|---|---|
| `0040` — one sampler serves the example, the differential and the tests | a second sampler, byte-identical, at `solution_proof.cpp:18-25` |
| `0041` — a subtask's constraint is typed bounds | a regex over the constraint sentence, `solution_proof.cpp:38-48`, which returns **10** for `a_i <= 10^9, n <= 2000` and **2** for `n ≤ 2·10^5` |
| `0042` — agreement means the same token sequence | whole trimmed strings, so `3 3\n` and `3\n3\n` disagree |

## 2. The decision

| What | The rule |
|---|---|
| the records | stay as written |
| the code | changes to match them |
| the three places | a typed constraint, one sampler, token agreement |
| every later step | starts from the records |
| a record that turns out wrong | is amended explicitly, in its own commit, with the measurement that proved it — never outrun by code that quietly did something else |

## 3. Why

The records are the operator's decisions, taken with the evidence in front of him. The code's shape came from a
session that did not have them.

Adopting the code would not even save the work: it keeps the hard part — recovering exact numbers from model
prose — and would still require making that parser correct. For the four sentences above it is not correct
today, so the choice is between a parser that must be fixed and a type that cannot be wrong.

## 4. Consequences

- The 33 call sites of the sentence constraint become typed bounds, and the parser is deleted rather than
  repaired.
- The differential compares tokens, and a cross-check keeps it equal to the judge's own token checker.
- The certifier's depth (the staged task that makes it re-run the differential) now depends on this landing
  first, because it re-runs that comparison.
- The content schema gains `subtasks`; until it does, a generated task with subtasks cannot be stored at all.

## 5. What was rejected

- **The records follow the code.** It would keep a parser measured to misread four of six realistic sentences,
  and ask a future maintainer to keep it correct forever.
- **Decide case by case.** Two shapes in one pipeline is the defect this record exists to remove.
