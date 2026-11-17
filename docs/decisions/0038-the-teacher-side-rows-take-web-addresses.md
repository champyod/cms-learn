# 0038 — The teacher-side rows take web addresses

Status: accepted 2026-11-17 · decision by the operator

## 1. The defect

Three rows about the teacher side were added with no block of their own, and they reuse numbers that already
mean other features:

| Row | Its number | What that number already means |
|---|---|---|
| a task's fields are data | `F-013` | limit measurement |
| the authoring form is generated from those rows | `F-014` | serving an unseen task |
| a task's bytes live in object storage | `F-015` | plagiarism detection |

`FEATURES.md:15-16` says ids are allocated by area, never reused, and that gaps are reserved rather than
lost. A duplicated address makes `F-013` mean two things, and the file is the inventory the coverage
decisions are read from — an ambiguous inventory cannot answer "what does this project cover".

## 2. Why the content block could not take them

Content owns `F-001–019`. `F-001–016` were taken, and the two features that landed on 2026-11-17 — the
statement step and its two leak checks — took `F-017` and `F-018`, which were the reserved gaps. One gap
(`F-019`) remained for three rows.

## 3. The decision

The three rows take **`F-032`, `F-033` and `F-042`** — the first three free addresses in the web block — and
they move under the Web section of the file.

They belong there because each is a surface the administrator uses: the field editor, the authoring form,
and the file a form field points at. The block that describes what an administrator operates is the honest
home for them.

## 4. What was rejected

- **A new block for teacher features (`F-080–089`).** Semantically cleaner, and it changes the filing rule at
  the top of the file — the line every other row is read against. A second change to that rule needs a
  reason of its own, and three rows are not it.
- **Leaving the duplicates and marking them by hand.** It keeps the rows where they are and leaves the
  address ambiguous, which is the defect.

## 5. Consequences

- No row changes state, wording or evidence in this move. Only the address changes, and the counts line is
  re-counted from the rows rather than adjusted.
- The web block now holds the authoring surfaces, so a later "authoring" area would have to move them again
  with a record of its own.
- The two features that landed first keep `F-017` and `F-018`: they are content features, and the gap they
  took was reserved for exactly that.
