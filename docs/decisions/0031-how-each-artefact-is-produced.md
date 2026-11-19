# 0031 — How each artefact is produced

Status: accepted 2026-11-15 · decision by the operator

## 1. The shape the repository already fixes

| What exists | Where | Format |
|---|---|---|
| a statement, one per locale | `content/tasks/<slug>/statement.<locale>.md` | markdown |
| the reference solution | `solutions/main.cpp` | one C++ file |
| examples | `examples/<n>.in`, `<n>.out` | plain text pairs |
| the test manifest | `tests/manifest.json` | JSON with a digest per case |
| the compile standard | `judge/include/cms/judge/run.hpp` | `c++17` unless a task declares one |

The pipeline produces **these shapes**, not new ones.

## 2. Each artefact, and how it is made

| Artefact | How | Language | What certifies it |
|---|---|---|---|
| **test-case generator** | **template** for the skeleton — it reads the declared bounds, prints the input shape, takes a seed — plus a **model-written** randomisation body | C++17 | compiles; the same seed twice gives identical bytes; the validator accepts its output |
| **validator** | **template**: testlib `readInt`/`readToken` calls generated from the declared bounds | C++17 | refuses every malformed input the bounds forbid, accepts the generator's |
| **checker** | **template**: testlib with the comparison rule the brief names | C++17 | the reference passes; a planted wrong answer fails |
| **reference solution** | **model, no template** — a template would yield a formula, not an algorithm | C++17 | passes every generated case at full score, inside the declared limits |
| **partial solution, one per subtask** | **model, one per subtask** | C++17 | passes its own subtask's cases and **fails the next subtask's** |
| **statement** | **fixed sections** (task, input, output, constraints, examples, scoring), the **model writes the prose inside each** | markdown, one file per locale | a leak gate; it never names the intended algorithm or `lean_to` |
| **examples** | **derived** from the stored brute-force program, run on the smallest case the bounds allow (`0034`), never written by the model | `.in` / `.out` | the reference reproduces each expected output exactly |
| **manifest** | **deterministic**, written by the verifier | JSON with digests | every digest matches the bytes |

## 3. The rule behind the split

**A template is right where the answer is a form; wrong where the answer is a thought; and never right
where the answer must be exact.**

- A **form**: the generator's skeleton, the validator, the checker, the statement's sections.
- A **thought**: the algorithm, the prose, the partial solutions that embody a weaker idea.
- **Exact**: the examples, the manifest, the digests — these are computed, and a model touching them is a
  defect rather than a shortcut.

## 4. Why partial solutions are generated

A subtask carries points because a weaker solution earns them. Nothing proves that today: a task can be
scored wrongly while every program in it is correct. One partial solution per subtask, proven to pass its
own group and fail the next, is what makes a subtask's weight a measurement rather than a claim.

## 5. What was rejected

- **Free-form statements.** Each would fit its task exactly, and two runs would differ in shape, so a reader
  hunts for the bounds and a gate cannot check a section exists.
- **Only the full-score reference.** Cheap, and the subtask weights would rest on an untested claim.
- **A model writing the manifest or the examples.** A digest a model wrote is a digest nobody computed.

## Interpretation added 2026-11-19 — two of the six sections are rendered, not written

§2 gives the statement six fixed sections and says "the model writes the prose inside each". Two of them are
now **rendered from rows**, not written:

- **constraints** — from the task's bounds and each subtask's typed narrowing (`0041`);
- **scoring** — from the subtask rows' points and constraints, which `0033` §4 already required: "the
  statement's scoring section is generated from these rows".

This is the shape of §4's own amendment: a value no gate can compute is a defect. A generator that still
asks the model for those two sections would end up with two texts for one field.

Whether a model answer that still supplies them is **refused** or **ignored** is the operator's open item;
it is not settled here. The sections themselves are rendered either way.
