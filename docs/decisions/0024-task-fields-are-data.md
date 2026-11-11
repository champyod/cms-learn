# 0024 — A task's fields are data, not code

Status: accepted 2026-11-11 · decision by the operator

## 1. What the other CMS does, and why it is not enough

The contest system in this workspace stores a task as `task_type` plus `task_type_parameters`, and each
type declares its fields as Python classes (`src/cms/grading/tasktypes/Batch.py:93-115`,
`src/cms/grading/ParameterTypes.py:42-190`, read back at `src/cms/db/task.py:433-495`). It has four field
kinds — text, integer, choice, collection — and five task types.

That is a good model and it stops at a wall: **adding a field kind or a task type is a code change and a
rebuild.**

## 2. The decision

The field definitions are **rows**, not classes:

- `task_field_defs` — one row per field of a task type: its key, its **kind**, its labels per language,
  whether it is required, its choices for a choice field, its nested fields for a collection, its bounds,
  and its position.
- `task_field_values` — one row per task version and field key, holding the value as `jsonb`.

The form and the validator both read the same rows, so a field cannot be rendered one way and checked
another.

| Kinds stored | text · integer · boolean · choice · collection · file |
|---|---|
| A new field | one row, no rebuild |
| A new task type | rows, no rebuild |
| Who may change a definition | `content:write`, through the panel |

## 3. What was rejected

- **Field kinds in code, as the contest system does.** Caught at compile time, and every new field or
  task type needs a rebuild and a deploy — which is the limitation this decision exists to remove.
- **A free-form `jsonb` blob per task with no definition.** No rebuild and no rows to maintain, but then
  nothing knows what a field means: the form cannot render it, the validator cannot check it, and the
  report of "what a task carries" is a guess.
- **Definitions in a tracked content file instead of the database.** Versioned with the repository, but
  an administrator cannot add a field, which is the point.

## 4. Consequences, stated rather than discovered

- **A wrong definition is a data mistake, not a compile error.** The definition tables therefore need the
  same checks the code used to get for free: a kind must be one of the six, a choice field must carry
  choices, a collection must name fields that exist, and a required field cannot be empty. These are
  constraints on the tables, not conventions in a reader.
- **Removing a field that holds values is refused**, not silently orphaned.
- **A definition change is an event worth recording**, because it changes what every task of that type
  means. It writes an audit row naming the actor, like every other state change in this repository.
- This record fixes the shape; it does not claim the pipeline that fills most fields exists. Fields that
  no step produces can be defined and left empty, and the form must say they are unproduced rather than
  pretend they are optional.
