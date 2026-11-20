# 0056 — A locale's name is content, not a constant in the generator

Status: accepted 2026-11-20 · decision by the operator

## 1. The question

Step 4 asks a model to write one locale's statement, and the prompt has to name the language it is asking
for. The step takes that name as data (`LocaleDeclaration`), because ADR 0031 §2 forbids it from knowing a
language — but the tree supplies no name to pass. `deploy/schema/types.sql:49` is
`CREATE TYPE locale AS ENUM ('en','th')`: a tag with no label. `content/` holds the vocabulary for topics,
levels, tracks and task types, and `content/locales.json` did not exist. Until t14 the worker could not write a
statement at all, so the gap was invisible; composing steps 1 to 7 made it the first thing the pipeline needs
and does not have.

## 2. The decision

| What | Where it lives now |
|---|---|
| the languages a task may ship | `content/locales.json`, beside `topics.json` |
| one language | a tag (`en`) and the name a prompt says (`English`) |
| who validates it | the content gate: the schema, no duplicate tag, no empty name, and every task locale must be declared |
| how the statement step learns a language | it does not: the runner reads the file and hands the step a `LocaleDeclaration` |
| how the deployment finds the file | through the content root the worker's configuration already names (ADR 0028) |
| the task schema's `locales` enum | removed; declared-ness is the gate's check, so the shipped set has one source |

The shape follows the vocabulary beside it: `{"version": 1, "locales": [{"tag": "en", "name": "English"}]}`.
`en` is first because ADR 0014 makes English the one locale every task carries.

## 3. Why

Every other label in this project already lives in `content/` — a topic carries its Thai and English names,
a task type carries its field labels — and the rule that makes that work is the same here: a surface renders a
label, never a compile-time constant. A language name is a label.

Putting it in the generator instead would have been the smallest change and the worst one: it would make
`statement.cpp` the place a language is decided, which 0031 §2 forbids in as many words, and it would mean a
new locale is a code change and a release. A file beside the vocabulary costs one line per language and keeps
the prompt data-driven.

Removing the task schema's `enum ["en","th"]` is part of the decision and not a tidy-up: with the enum left in
place a locale added to `content/locales.json` would still be refused by the schema, so the gate would pass
while the acceptance — one source of truth for the shipped set — was false.

## 4. Consequences

- A new locale is one entry in `content/locales.json` plus its `statement.<tag>.md` files; no code changes and
  no rebuild of the worker's logic.
- The runner refuses a task whose statement locale the file does not declare, before a prompt is built, naming
  the tag and the file.
- The database's `locale` enum stays the two-value set it is. The gate cannot check that it matches this file
  while `deploy/schema` is outside the change that added the file, so the duplication is recorded here and
  belongs to a task of its own: the database and the content must agree, and today nothing proves they do.
- The worker's configuration is unchanged: the content root was added for the vocabulary in t14, and this file
  is read through the same root.

## 5. What was rejected

- **The tag standing in for its own name.** `write in en` is a prompt that names a code, not a language, and
  the model that receives it is being asked to guess what the operator meant.
- **A language list in the generator.** One constant is all it takes for a second language to need a release,
  and 0031 §2 forbids the step from holding it at all.
- **A `locales` table in `deploy/schema/`.** It would put a label in the database to join against on every
  statement, when the label is content that ships with the image and changes with it.
