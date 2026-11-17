# 0037 — The leak checks count characters in Thai and read the title as well as the statement

Status: accepted 2026-11-17 · decision by the operator

## 1. Two rules the record left without a unit and without a surface

- `docs/task-generation.md:190` says a "verbatim span of eight words". Thai writes words without spaces:
  the repository's own Thai statement carries ten whitespace tokens for a longer sentence
  (`content/tasks/grid-shortest-path/statement.th.md:3`), and no word segmenter is in the dependency
  allowlist (`docs/dependencies.md:143` is the Thai font). A rule counting words therefore has no word to
  count in Thai — it would pass a copied Thai sentence.
- The same row reads "the statement files", while the page title is generated first (`0014` §2) and
  rendered to students (`api/src/task_view.cpp:34-35`). A title that names the intended approach reaches a
  student past both checks. The story has the same exposure and no storage or schema home yet.

## 2. The decision

| Language | Unit | Threshold | Why that number |
|---|---|---|---|
| English | one word | 8 words in a row | the rule the document already states |
| Thai | one character | 32 characters in a row | a Thai word averages about four characters, so eight words land near thirty-two |

The checks read the **title** and the **statement**, in every locale a task ships. The story stays out of
scope until it is built, and the record that builds it owns adding it.

## 3. What the check compares, and what it never prints

- The request: the brief and the admin's one-line description, both from the job row (`0036` §2).
- The plan: the four texts the chosen candidate row holds — its approach, its intended complexity, the
  plan itself, and the judge's reason (`deploy/schema/generation.sql:73-86`). A reason that says why a plan
  is worth building describes the answer as well as the plan does.
- A refusal quotes **the statement's** span only. It never prints the request's surrounding text, so a log
  line or a generation record cannot become the request's second copy (`0035` §5).

## 4. Consequences

- One rule with two units. The Thai threshold is a number written here, never derived from the English one
  at run time.
- Text is normalized before it is windowed: whitespace runs, markdown emphasis, case and Unicode combining
  order. Without that, a line break inside a copied span hides it.
- The title joins both checks, so it becomes an input to the statement step rather than a separate write
  nobody reads.
- `0035` §6's "any exit path" is corrected here: the **caller** owns the temporary directory, because a
  destructor does not run after a kill. A crashed check leaves its directory to the parent, which removes it.
- A Thai sentence and an English sentence cannot match verbatim, so the translation is guarded by the
  algorithm rule and by the request being compared in the language it was written in, not by expecting a
  cross-language match.

## 5. What was rejected

- **A Thai word segmenter.** Its output would decide what counts as a word, and the rule would then depend
  on a component this project does not gate.
- **English only, with Thai recorded as unchecked.** It leaves the language most students read with no
  comparison at all.
- **Statement files only, with the title out.** It keeps the check cheap and leaves the page heading
  unguarded, which is where a one-line answer is easiest to put.
