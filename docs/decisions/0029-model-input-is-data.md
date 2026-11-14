# 0029 — Model input is data, and the answer is checked against the records

Status: accepted 2026-11-14 · decision by the operator

## 1. What the security review found

Step 1 sends a teacher's free-text brief to the model verbatim, and step 2 sends step 1's plan JSON the
same way. A brief can therefore contain instructions that steer which candidate the judge chooses. The
review classified it as OWASP LLM01 and noted what still holds: `fits_bounds` cannot be flipped, so the
limit refusal survives — the *choice* is steerable, not the safety property.

## 2. The decision

Two rules, both enforced in code rather than by asking a model to behave:

| Rule | What it means |
|---|---|
| **model input is data** | a teacher's text and a previous step's output are framed as data with an explicit boundary, never concatenated into an instruction |
| **the answer is checked against the records** | whatever the model returns is validated against the brief, the candidate set and the schema before a row is written — an answer that names a plan nobody offered, or a plan that does not fit the declared bounds, is refused |

The second rule is the load-bearing one: framing reduces the chance of steering, and validation makes
steering harmless. A model that has been talked into choosing the wrong plan cannot write it, because the
choice must be a position that exists and a plan that satisfies the brief.

## 3. What was rejected

- **Record it as an accepted risk.** Cheapest, and the limit refusal genuinely cannot be flipped. Rejected
  because the operator's stated position is that nothing gets through, and because this is the one place
  where a *teacher* writes the text the model reads — the trust boundary is inside the product, not outside.
- **Ask the model to ignore instructions in the data.** A prompt is a request, not a barrier, and it would
  make the safety property depend on the model's cooperation.
- **Strip instruction-like text from the brief.** Removes the injection and also removes a teacher's right to
  write a brief in their own words, and it is a filter that will always be incomplete.

## 4. What this obliges

Every later step that sends text to a model — statement, solution, tests, certifier — inherits both rules,
and each step's test carries a steered input that must be refused rather than obeyed. A step that cannot
show that is not finished.
