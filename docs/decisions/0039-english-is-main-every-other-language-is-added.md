# 0039 — English is the main language; every other language is added and written natively

Status: accepted 2026-11-17 · decision by the operator

## 1. The contradiction this record settles

Two accepted records disagreed about whether a generated task must ship Thai:

- `0015` §1, the step table: step 4 produces a "statement in TH and EN".
- `0014` §2, the authoring form: "English always; Thai optional".

The review of the statement step found the consequence: the step accepted a declared language set while both
gates demanded Thai, so a task that declared less could not pass.

## 2. The decision

| What | The rule |
|---|---|
| the main language | English, always required |
| every other language | an **added** language, at the same level as any other, searchable and addable — nothing in the product is dedicated to Thai |
| Thai | suggested first by the authoring surface, because it is the most-used language here — a **hint**, never a rule |
| each language's text | written in that language by its own call, **never produced as a translation** of another |
| the mechanism | one and generic: a language is data; no constant, branch or function names a language except the required English tag |
| what the checks read | every language the task declares |

## 3. What this changes in the existing records

- `0015` §1's "statement in TH and EN" is read as the common case, never a requirement. A task declares the
  languages it ships, and the pipeline writes exactly those.
- `0037`'s unit table — eight English words, thirty-two Thai characters — holds for scripts of those shapes.
  A language of another shape (no spaces between words, as Chinese or Japanese) needs its own unit, named in
  the record that admits it. The unit is never inferred from the language's name.

## 4. The consequence that is not yet built

`task_statements.locale` is the two-value enum `en | th` (`deploy/schema/types.sql:49`, used by
`deploy/schema/content.sql:64`). A task that declares a third language is written correctly by the step and
cannot be stored. Growing that enum is a schema step of its own, and it is the next thing this decision owes.

## 5. What was rejected

- **Thai always**, which is `0015`'s literal reading. It forces a Thai statement onto a task that never asked
  for one, and it makes the platform Thai-dedicated against the operator's ruling.
- **Exactly `{en, th}`**, the gates' current demand. It is the same rule under a different name, and `0014`
  does not have it.
- **Translating from English.** A translated statement has no writer of its own, so a leak or a mistake in the
  source text is copied into every language — and the leak checks would then compare a language against a
  text that never carried that language's wording.
