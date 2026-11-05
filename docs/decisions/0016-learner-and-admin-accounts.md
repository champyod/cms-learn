# 0016 — Learner and administrator are two account tables

Status: accepted 2026-11-06 · decision by the operator

## 1. Two tables, and one person is one or the other

A learner and an administrator are separate accounts with separate ids:

| | `learner_accounts` | `admin_accounts` |
|---|---|---|
| arrives by | self-registration, then approval | the command line, or an existing administrator |
| e-mail | required — it carries verification and password reset | **none** |
| handle | chosen by the learner | chosen by whoever creates the account |
| lifecycle | `pending → active → suspended → closed` | exists, or is revoked |
| permissions | none | roles holding capabilities |
| learner data | level id, team | none — an administrator has no learning path |

`principals` and its `kind` column (`user`, `system`) are replaced. The `system` principal stays as a
system account row, because an audit event from the seeder or a plan apply still needs an actor.

## 2. What this costs, stated before it is done

`principals` is the target of thirteen foreign keys: `audit_events`, `sessions`, `principal_roles`,
`roles`, `progress`, `submissions`, `tokens`, and the three audit columns in `settings`. After the
split every one of those has to name the table it points at:

| Group | Points at |
|---|---|
| `progress`, `submissions`, `tokens` | `learner_accounts` |
| `principal_roles`, `role_capabilities`, the `granted_by` on a role | `admin_accounts` |
| `sessions` | **either** — an account kind column beside the id |
| `audit_events`, and the three audit columns on `settings` | **either** — the same column |

That column is a real cost of the split, not a detail: **every audit row must say which kind of
account acted.** It is one small column, and it is unavoidable, because two ids from two tables are
otherwise indistinguishable.

Because an administrator has no e-mail, an e-mail exists in one table only, so **no shared login row is
needed** and no cross-table uniqueness rule is needed. The two tables stand alone, which is what the
operator asked for.

## 3. How an administrator is created

Only two paths exist, and neither is self-registration:

- the command line;
- an existing administrator, with the capabilities they hold.

The creator chooses how the new administrator gets in:

1. a one-time setup link, copied and sent by hand;
2. a one-time setup link sent by e-mail — or, when e-mail is not configured, the field is **disabled and
   says why** rather than failing silently;
3. a typed password, or a generated one.

A learner's approval stays what `0002-theme-and-auth.md` decided: registration creates a `pending`
account, and an administrator activates it. That decision is unchanged.

## 4. What was rejected

- **One identity table with a learner flag.** Only one round of decisions earlier this was the
  recommendation; the operator chose otherwise, and the reason is sound: a learner and an
  administrator share no permissions at all, so a shared table forces every reader to ask "roles or
  not" to answer a question that should be a fact.
- **Two identity tables with no audit-kind column.** Cheap here, impossible later: after the split two
  ids from different tables cannot be told apart, and an audit trail that cannot name its actor's kind
  cannot be filtered safely.
- **A shared login row behind both tables.** Would let one e-mail exist once across both. Not needed,
  because an administrator has no e-mail, and it would add a third table that sessions and audits
  actually point at.

## Consequences

- One migration renames `principals` and rewrites thirteen foreign keys; the plan must be rendered and
  every gate re-run, not hand-edited.
- A teacher who also solves problems needs two accounts. With lessons learned from `0008-no-modules.md`,
  the honest consequence is written here rather than discovered later.
- The learner's level id lands in `learner_accounts`, beside the account that has a learning path,
  which is what `0013-difficulty-rungs.md` and the level table were prepared for.
- `docs/content-model.md` and the documents that describe an account need a sentence each once the
  migration lands; neither is edited here.
