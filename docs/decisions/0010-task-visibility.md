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
