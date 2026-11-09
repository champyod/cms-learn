# 0020 — One pattern for recording who acted

Status: accepted 2026-11-09 · decision by the operator

## 1. The defect class

Four tables recorded an actor as a bare uuid with a foreign key to `admin_accounts`:

| Column | Was |
|---|---|
| `settings.updated_by` | `uuid NOT NULL REFERENCES admin_accounts` |
| `settings_plans.author_id` | `uuid NOT NULL REFERENCES admin_accounts` |
| `elevations.granted_by` | `uuid NOT NULL REFERENCES admin_accounts` |
| `elevations.approved_by` | `uuid REFERENCES admin_accounts` |

Once the administrative socket began acting as the **system** account (`0019-admin-socket-identity.md`),
the database refused every settings and elevation action arriving from the command line: the system
account is not an administrator, so the foreign key rejected it. `settings_functions.sql` also stamped
the audit row with a hardcoded `'admin'`.

This is the same defect as the request-supplied actor (`0019`) seen from the other side: **an actor was
recorded in a shape that could only describe one kind of account.** The project had already solved it
once, correctly, in `audit_events` — kind plus one id column per kind — and five other places had not
been brought along.

## 2. The decision

Every column that records who acted uses the `audit_events` pattern, and no other:

```
actor_kind    account_kind  NOT NULL,
learner_actor_id uuid REFERENCES learner_accounts (id),
admin_actor_id   uuid REFERENCES admin_accounts (id),
system_actor_id  uuid REFERENCES system_accounts (id),
CHECK exactly the id column matching the kind is set
```

Applied to `settings.updated_by`, `settings_plans.author_id`, `elevations.granted_by` and
`elevations.approved_by`. Each per-kind column keeps its own foreign key, so referential integrity is
preserved rather than traded away: a learner actor must still be a real learner.

`elevations` keeps its two-person rule and its grantor-cannot-approve rule, and both now compare **kind
beside id**, because the same uuid in two tables is two different accounts.

`current_actor()` returns a composite `actor_identity(kind, id)`, reads `cms.actor_id` **and**
`cms.actor_kind`, and names whichever session variable is missing rather than reporting one of them.

## 3. The rule that follows

**No function may infer an account kind from an id.** The `Actor` is produced by the surface that
authenticated the caller and passed down; the database stores the kind it was given.

This project has now hit the inference defect three times — the socket trusting a request, the approval
row hardcoding `admin`, and a password row naming the account whose password changed as its own actor.
The rule exists so the fourth instance cannot be written.

## 4. What was rejected

- **Keep the columns administrator-only and have the socket act as a bootstrap administrator.** No
  migration, but the audit trail would name a person who did not act, which is the lie `0019` exists to
  remove.
- **One `actor_id` uuid with no foreign key at all.** Simplest schema, and it silently loses
  referential integrity: a settings row could name an account that never existed.
- **A separate audit table per account kind.** Three tables with identical columns, and every reader
  would have to union them.

## Consequences

- The rendered plan and the generated table header are regenerated, not edited: `842`-line plan SQL.
- `elevation.cpp` and `settings_plan.cpp` each split at their seam into a body and a `*_state.hpp`,
  because the change pushed both over the 250-line gate.
- `system:bootstrap` and the account-kind strings now have one declaration each, in `accounts.hpp` and
  `actor.hpp`, instead of a copy per translation unit.
- Three registered shell tests still seed the removed `principals` table (`submit_e2e.sh`,
  `task_route_e2e.sh`, `settings_route_test.sh`). They are pre-existing staleness from `0016` and need
  their own step; this record does not claim they pass.
