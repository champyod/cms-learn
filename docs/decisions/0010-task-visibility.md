# 0010 — Task visibility as one encoded principal column

Status: accepted 2026-11-04 · decision by the operator

## 1. One row per principal, kind encoded in the number

A task's audience is a set of principal IDs in `task_visibility(task_id, principal_id)`. The kind is
carried in the value, not in a second column:

| Value | Means |
|---|---|
| `0` | everyone |
| `1·2^48 + user_id` | a learner |
| `2·2^48 + admin_id` | an administrator |
| `3·2^48 + team_id` | a team |

`2^48` is about 2.8×10^14, far beyond any identifier this system will issue. A `CHECK` keeps values
inside the four legal ranges, and the primary key is `(task_id, principal_id)`, so the same grant
cannot be written twice.

## 2. The query is one set membership

```sql
EXISTS (SELECT 1 FROM task_visibility v
        WHERE v.task_id = t.id
          AND v.principal_id = ANY($principals))
```

`$principals` is assembled once per request as `{0, this user, this user's teams}`. A task visible to
everyone needs one row (`0`); a task nobody but an administrator may see has no rows at all.

Why not `kind` plus `id`: that forces the query into several `OR` branches over two columns that must
stay consistent with each other. Why not an array column: adding a viewer rewrites the task row, and
the list grows without bound. Why not `u123` text keys: wider index, and every comparison needs
parsing first.

## 3. Defaults per author

- A task created by a learner carries the `0` row: visible to everyone.
- A task created through the admin page — imported, written by hand, or generated — carries no rows:
  visible only to an administrator who holds `tasks:read`.

The kind encoded in a principal ID is **not** a permission. Seeing every task comes from the
permission, never from being an administrator, because an administrator without `tasks:read` must not
see what a learner cannot.

## 4. The cost we accept

Two helpers, in one module, build and decode a principal value. Nothing else may compute `2^48`.
Without that rule the encoding spreads across the codebase and a single wrong shift is invisible
until a viewer disappears from a list.

## 5. Serving needs a principal (operator decision, 2026-11-04)

An anonymous request has **no audience**. `{0}` alone is not a set of principals a serving request runs
with: it is one row a task may carry, and a request that carries no principal has no business being
compared against it.

- `GET /tasks/{id}` requires an **approved, signed-in** account. No session, an expired or revoked
  session, and a session whose account is not active all answer 303 to `/signin`: they are
  unauthenticated, not forbidden. The page route and `POST /tasks/{id}/submit` resolve the session
  through the same helper, so the two cannot drift.
- A task with **no visibility rows** (the administrator-created default in §3) is visible only to a
  session holding `content:read`. That is the permission §3 names (`tasks:read` in the operator's
  wording); being an administrator is not the trigger, and an administrator without the capability
  still sees no hidden task. The capability check and the audience check are the same read: a row
  matches the audience, or the viewer holds the capability.
- The page renders the **published version** — the highest `published` version of the task, the same
  one the submission write stores against — so a version that is being edited is never half-rendered.
- `/tasks/{id}/verdict`, which the page polls, answers under the same session and visibility rules,
  and uses the page's one indistinguishable 404 body. A server error answers one generic body that
  names neither the principal nor the task.
