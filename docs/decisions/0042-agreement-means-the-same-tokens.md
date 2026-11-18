# 0042 — Agreement means the same tokens, after trailing whitespace is trimmed

Status: accepted 2026-11-18 · decision by the operator

## 1. The question

The records require the reference solution to "agree with a brute force" on small inputs
(`docs/content-model.md:61-66`, `0015` §1 row 5), and they define neither the object compared nor the
comparison itself. Two correct programs can differ by a trailing space; a wrong one can print the right
values in a layout the statement never asked for.

## 2. The decision

| What | The rule |
|---|---|
| the values | the same **token sequence**, a token being a maximal run of non-whitespace |
| whitespace | trailing whitespace on each line and at the end is trimmed before tokenising; a run of whitespace between tokens is one separator |
| the layout | **not** compared — where a line breaks is not part of the answer |
| the exit status | must be zero; a non-zero exit on a valid input is a refusal, not a disagreement |
| the time | not compared here; the limit is its own method (`F-013`) |

## 3. Why

Byte equality refuses a correct program over a trailing space, which teaches the operator to distrust a
gate that is right. Counting tokens without comparing them accepts a matrix printed as one line, which is
a wrong answer wearing the right shape. The statement promises values, not layout, so the differential
compares values.

The repository already ships a token checker for the judge (`judge/checkers/token.cpp`), so the
differential and the judge can apply the same rule instead of two definitions of "same answer".

## 4. What was rejected

- **Exactly the same bytes.** It converts a formatting difference into a false refusal.
- **The same number of tokens in any layout.** It accepts an answer with the right count and the wrong
  structure.

## 5. Consequences

- The comparison is a function of two outputs, testable without a database or a compiler.
- A task that genuinely promises an exact layout needs its own statement rule and its own checker; this
  record says the differential does not judge layout, and a task that needs it must say so.
- The refusal names the input, the reference's line and the brute force's line, so a reader sees the
  disagreement rather than a verdict.
