# 0051 — The oracle is checked against an answer the model gave before it was written

Status: accepted 2026-11-19 · taken under the operator's autonomous window, listed for his review

## 1. The defect, verified in the tree

The pipeline's chain of trust is circular today:

| Where | What it does |
|---|---|
| `generator/src/solution.cpp:27-40` | the reference prompt **includes the oracle's source** and says "agree with the oracle on every case" |
| `generator/src/oracle.cpp:145-152` | `check_oracle` compiles the program and runs it on the smallest case; it returns whatever the program printed |
| `generator/src/solution_proof.cpp:88-105` | the differential compares the reference against that same oracle |
| step 4 (`0034`) | the statement's example prints that oracle's answer |
| step 6 (uncommitted) | each expected answer comes from that oracle |

Every check is the oracle agreeing with itself. An oracle that is off by one is therefore baked into the
example, the reference and every expected answer at once — and `docs/content-model.md:63-65`'s promise that
this check "catches a wrong checker or a mis-generated expected output" does not hold.

This is the project's own recurring defect class: **a proof that supplies its own conditions**.

## 2. The decision

| What | The rule |
|---|---|
| the independent answer | step 3 answers the output for **one pinned input** the specification names, before it writes the oracle |
| the check | that same input runs through the oracle; the two answers must agree under `0042`'s comparison, or the task is refused by name |
| the separation | the oracle is written after the answer, and the answer is not shown to the oracle's author |
| a disagreement | refused, naming the input and both answers |

## 3. Why

Two paths to one fact that do not share a source. A model answering one small input **by hand** is not the
program that computes it — that is the "a different question, not a vote" rule `0015` §2 already states. The
differential stays: the reference is still compared with the oracle on many inputs. What changes is that the
oracle itself is now checked before anything inherits its answers.

## 4. Consequences

- The oracle's first use becomes a test **of** the oracle rather than a claim **about** it.
- A wrong oracle is caught at step 3, before the example (`0034`), the reference (step 5) and the tests
  (step 6) inherit it.
- The hand answer is recorded as the answer artefact (`0030`), so a later reader sees both answers and the
  input that separates them.
- It costs one more model answer per task, and it re-uses the input the oracle check already runs on — the
  smallest case — so no new sampling is needed.

## 5. What was rejected

- **Asking two models to agree.** That is a vote, and `0015` §2 forbids votes as evidence.
- **Trusting the oracle because it is "brute force".** Brute force describes how it computes, not whether it
  is correct; trusting it is a claim, and this project records evidence.
