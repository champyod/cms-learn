# 0025 — Who may author, and the role that makes any capability reachable

Status: accepted 2026-11-11 · decision by the operator

## 1. The blocker this record closes

`content:write` is declared in `deploy/schema/types.sql` and `core/include/cms/capability.hpp` and is
authorized by **no code path**. Worse, `cms::api::bootstrap` creates the first administrator and grants
it **no role at all** (`api/src/accounts.cpp:408-441`), and no role is inserted anywhere outside test
fixtures. So in a fresh install **no administrator holds any capability**, and a grant route would be
unreachable — the same shape of defect as the bootstrap that nothing called.

## 2. The decision

Two roles, both seeded by the first-run path, idempotently:

| Role | Holds | Granted to |
|---|---|---|
| `operator` | every capability | the first administrator, created by the first-run path |
| `author` | `content:read`, `content:write` | nobody by default; an administrator grants it |

The grant is a panel action: `POST /admin/grant-role`, behind the panel listener, an administrator
session, the per-session CSRF token, and `user:write`. It writes `admin_roles` and one audit row naming
the actor.

`author` is not granted automatically. Authoring content is a task for a person who was trusted with it,
not a property of being an administrator.

## 3. What was rejected

- **Every administrator holds `content:write` implicitly.** No seeding and no grant route, and every
  administrator — including one hired for something unrelated — can write content.
- **Nobody holds it; an operator inserts the role by hand.** Honest and minimal, but the form refuses
  everyone until someone runs SQL, which is the defect this record exists to remove.
- **`author` granted to the first administrator automatically.** Convenient, and it silently makes the
  bootstrap identity an author whether or not that was wanted.

## 4. Consequences

- The first-run path now seeds two rows and one grant. It stays idempotent: a second start writes nothing.
- A fresh install can author, and cannot author until someone is granted the role.
- `operator` is a named set of every capability. Adding a capability later means deciding whether
  `operator` carries it — the seed names the capabilities explicitly rather than meaning "all".
