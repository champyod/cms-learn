# 0047 — A case is generated for a group, and the group exists before the case

Status: accepted 2026-11-18 · decision by the operator

## 1. The correction this record makes

I asked how a case would *declare* its group after being generated — which quietly assumed cases come first
and are sorted afterwards. The operator corrected the order: the groups come from the theory, and the cases
are generated **to fit a group**. A case is never written first and assigned later.

## 2. The decision

| What | The rule |
|---|---|
| where the groups come from | the theory — each subtask's typed narrowing (`0041`), which exists before any case |
| when a case's group is fixed | at generation; a case is **born into one group** and recorded with it |
| what a group holds | several ordinary cases, **and** several that a weaker idea cannot pass |
| sorting afterwards | none — nothing assigns a case to a group after the fact |
| a case that would satisfy two narrowings | it still belongs to the one group it was generated for, and counts there only |

## 3. Why

Generating first and sorting later makes a case's group the output of a heuristic: change the heuristic and
history re-scores, and nobody can say whether the group measured what the theory promised. Generating **for**
a group makes the group's promise measurable — its points are earned by the cases the theory said would need
the better idea.

The second half of the rule is what makes a subtask worth points at all: `0031` §4 already requires one
solution per subtask that passes its own group and fails the next one. A group of only easy cases cannot
fail that weaker solution, so it cannot prove its own weight. Edge cases are what give that assertion teeth.

## 4. Consequences

- The test manifest gains the group's identity on each case, and the certifier (`0046`) records the mapping
  it certified.
- The generator is handed a group and its narrowing; it never invents a group and never re-groups cases.
- Each group carries both ordinary and edge cases; how many of each is the count `0044` records.
- A subtask with no case of its own is an empty group, and the partition rule already refuses a set that
  does not add up.

## 5. What was rejected

- **Assigning cases to groups after generation** by matching their constraints.
- **Counting one case for every group it satisfies** — one case paying twice makes the scores stop adding
  to the task's total.
