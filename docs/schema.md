# Schema

`deploy/schema/` is the only DDL in the repository, linked by `main.sql` and rendered by pgschema.
`deploy/roles.sql` runs before every apply, because a grant naming a role that does not exist fails
the whole migration.

## From source to database

One file is edited, three are committed, and nothing is applied by hand:

| Step | Command | Produces |
|---|---|---|
| edit | — | `deploy/schema/*.sql` |
| render | `pgschema plan … --output-sql --output-json` | `deploy/plan/0001_init.sql`, `.json` |
| seal | `sha256sum … > deploy/plan/CHECKSUMS` | the digests of those two files |
| apply | `tools/apply-plan.sh` | the schema, in the target database |

Rendering needs a target database to diff against and a second database for the plan scratch space.
The embedded plan server hangs on this host — it leaves an empty `/tmp/embedded_postgres_log*` and no
process — so both are passed explicitly:

```bash
pgschema plan --host "$PGHOST" --port "$PGPORT" --db cmslearn --user postgres \
  --plan-host "$PGHOST" --plan-port "$PGPORT" --plan-db cmslearn_plan --plan-user postgres \
  --file deploy/schema/main.sql \
  --output-sql deploy/plan/0001_init.sql --output-json deploy/plan/0001_init.json
```

A render is deterministic: two renders of the same source produced byte-identical SQL, and identical
JSON except `created_at`. The plan is therefore compared field by field with that field ignored, which
is what `tools/compare-plan.ts` does.

### Why applying goes through a wrapper

pgschema does not verify the plan it executes, and the apply is no longer bound to a stored plan: it is
desired state (`pgschema apply --file deploy/plan/0001_init.sql`), so a database is brought to what the
source declares. A plan rendered for one database could not be re-run and could not upgrade an existing
one, which is why the mode changed ([0063](decisions/0063-the-schema-is-applied-as-desired-state.md)).

The wrapper matters for the same reason it always did. A step edited to declare `text` where the source
declared an enum failed only when PostgreSQL rejected the resulting foreign key (`SQLSTATE 42804`); an
edit that stays valid SQL — a weakened grant, a dropped constraint, a renamed index — would have applied
silently.

`tools/apply-plan.sh` recomputes the digests against `deploy/plan/CHECKSUMS` and refuses before
connecting if they disagree. Every apply path calls it — the schema container, `make upgrade`, and a
manual run — so the reviewed file is the only file that can reach a cluster.

`check:schema` proves both halves hold: the committed pair still equals a fresh render of
`deploy/schema/main.sql`, and applying it leaves nothing to change (`No changes detected.`).
`check:schema:falsify` breaks the plan, the digests, and the source in seven ways and requires the
specific refusal each time, because a harness that crashes also exits non-zero.

## Tables

| File | Holds |
|---|---|
| `types.sql` | the closed vocabularies: `setting_kind`, `setting_tier`, `account_kind`, `admin_status`, `principal_status`, `actor_identity`, `capability`, `audit_operation`, `token_purpose`, `task_status`, `task_verification_status`, `task_checker_kind`, `task_origin_kind`, `locale`, `verdict_status` |
| `authority.sql` | `admin_accounts`, `learner_accounts`, `system_accounts`, `roles`, `role_capabilities`, `admin_roles`, `sessions`, `elevations`, and their live-row indexes |
| `audit.sql` | `audit_events` with a kind beside each actor id, and the append-only guard |
| `settings.sql` | `settings`, `settings_meta`, `settings_plans`, `settings_audit` |
| `settings_functions.sql` | the functions the settings triggers need, including `current_actor()` returning kind and id |
| `settings_triggers.sql` | the settings triggers |
| `content.sql` | `topics`, `tasks`, `task_topics`, `task_statements`, `task_examples`, `content_seed`, and the published-row guard |
| `curriculum.sql` | `tracks`, `track_stages`, `stage_tags` |
| `levels.sql` | `levels`, `level_titles`, and the trigger that refuses a partial scale |
| `fields.sql` | `task_types`, `task_field_defs`, `task_field_values`, and the triggers that carry the checks a compiler used to give |
| `generation.sql` | `generation_state`, `generation_jobs`, `generation_candidates`, `generation_artefacts` |
| `progress.sql` | `progress` — what an account has touched, one row per account and task version |
| `submissions.sql` | `submissions` |
| `tokens.sql` | `learner_tokens` |
| `visibility.sql` | `task_visibility` |
| `grants.sql` | role grants, row-level security, and the `PUBLIC` revokes |

Note the names: an account is a `learner_accounts`, `admin_accounts` or `system_accounts` row, not a
`principals` row — that table was replaced in `docs/decisions/0016-learner-and-admin-accounts.md`, and
`principal_status` survives only as the learner status vocabulary.

## Invariants

1. **The system account cannot hold a session.** Three independent barriers: its row carries no
   password hash and no address, `sessions` names one account kind and exactly one id column with a
   foreign key per kind, and the sign-in query filters on the kind it serves. Any one of them would do;
   a future code path that forgets the filter still cannot mint one.
2. **A handle carries no colon unless it is the system account.** `system:bootstrap` occupies a namespace a
   human cannot register, so a log line naming it is never confused with an ordinary account.
3. **A value cannot disagree with its kind.** `settings_value_matches_kind` is a single CHECK whose
   nested CASE proves, per branch, that the text is a boolean, an in-range int64, or free text.
4. **The actor comes from the transaction, never from the row.** `SET LOCAL cms.actor_id` is required;
   `current_actor()` raises when it is absent, so an unattributed write is impossible rather than
   merely discouraged.
5. **A secret never leaves unredacted.** The audit trigger replaces the value with `<redacted>` when
   the row's tier is `secret`, and row-level security hides the whole row from `cmslearn_judge`.
6. **A notification carries the version and nothing else.** `pg_notify('cms_settings', version)` — no
   key, no value, so no secret can appear in a payload.
7. **The audit row cannot be forged or altered.** It is written by a `SECURITY DEFINER` trigger while
   the application role holds only `SELECT`; `UPDATE` and `DELETE` are refused by a trigger for the
   owner too.
8. **History outlives the row.** `settings_audit` carries no foreign key to `settings`, and
   `audit_events` none to anything it describes, so deleting a setting or an account leaves the record
   of what happened intact.

## Query layer

`db/include/cms/db/tables.hpp` declares the tables sqlpp23 checks against, and `tools/render-tables.sh`
writes it from `information_schema.columns` on a cluster that has the plan applied — the schema is the
input, so a column cannot go missing quietly. `db/include/cms/db/settings_query.hpp` holds the
statements. Both compile clean under `-Wall -Wextra -Werror`, and `check:db` fails when the committed
header is not what the schema renders.

Three rules that fell out of the library's contract, each learned from a compiler error:

1. A standalone name tag uses `SQLPP_CREATE_NAME_TAG(name)`; the `..._FOR_SQL_AND_CPP` form defines a
   namespace-scope `_sqlpp_name_tag` and therefore cannot appear twice in one namespace.
2. The C++ member name is the macro's **second** argument, so a column is `valueText`, `learnerId`,
   `expiresAt` — the SQL name stays snake_case and only the C++ side is camelCase.
3. `sqlpp::current_timestamp` is not comparable to a column, so "now" is a bound parameter. That is
   also the better contract: the clock becomes an explicit input a test can pin.

Two decisions live in the statement rather than in its caller. The snapshot query reads `tier` instead
of filtering it, because the process holding the snapshot decides which tiers it may carry and a
`secret` value must be dropped before it is ever built in. The session query carries `revokedAt IS
NULL` and the expiry comparison, so a revoked session cannot be resurrected by a forgotten check.

The execution side is `db/`: a CRTP connection over `drogon::orm::DbClient` that serialises a statement
to `$n` placeholders, binds the parameters by the order sqlpp23 walks them, and decodes the driver's
text rows into the types the header declares. `cms::db` stays an interface target so a consumer that
only names a column never inherits Drogon; `cms::db_drogon` is the compiled connector.

It supports `_select` and `_execute` only. Insert, update and prepared statements are deliberately
absent, so attempting one is a compile error rather than a silent no-op, and they arrive with E3, the
first code that needs them.

## Why the two definer functions exist

`settings_write_audit()` and `settings_bump_version()` run with the owner's rights, because the
application must write a setting without being able to write its audit row or set the version counter.
Granting either of those to the application would mean a bug in the service layer could forge history
or force every listener to reload. Both are revoked from `PUBLIC` and reachable only through triggers.
