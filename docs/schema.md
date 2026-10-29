# Schema

`deploy/schema/` is the only DDL in the repository, linked by `main.sql` and rendered by pgschema.
`deploy/roles.sql` runs before every apply, because a grant naming a role that does not exist fails
the whole migration.

## Tables

| File | Holds |
|---|---|
| `types.sql` | the closed vocabularies: `setting_kind`, `principal_kind`, `principal_status`, `capability` |
| `authority.sql` | `principals`, `roles`, `role_capabilities`, `principal_roles`, `sessions`, `elevations` |
| `audit.sql` | `audit_events`, and the append-only guard |
| `settings.sql` | `settings`, `settings_meta`, `settings_plans`, `settings_audit` |
| `settings_functions.sql` | the four functions the settings triggers need |
| `settings_triggers.sql` | the four triggers |
| `grants.sql` | role grants, row-level security, and the `PUBLIC` revokes |

## Invariants

1. **A system principal cannot have a session.** Three independent barriers: no password hash and no
   email on the row, the composite foreign key `(principal_id, principal_kind)` with the column pinned
   to `'user'`, and a login query filtered on `kind = 'user'`. Any one of them would do; a future code
   path that forgets the filter still cannot mint one.
2. **A handle carries no colon unless it is a system principal.** `system:root` occupies a namespace a
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
   `audit_events` none to anything it describes, so deleting a setting or a principal leaves the record
   of what happened intact.

## Query layer

`core/include/cms/db/tables.hpp` declares the tables sqlpp23 checks against, in exactly the shape
`sqlpp23-ddl2cpp` emits. `core/include/cms/db/settings_query.hpp` holds the statements. Both compile
clean under `-Wall -Wextra -Werror`, the SQL they produce runs against the real schema, and renaming a
column in the header breaks the build rather than production.

Three rules that fell out of the library's contract, each learned from a compiler error:

1. A standalone name tag uses `SQLPP_CREATE_NAME_TAG(name)`; the `..._FOR_SQL_AND_CPP` form defines a
   namespace-scope `_sqlpp_name_tag` and therefore cannot appear twice in one namespace.
2. The C++ member name is the macro's **second** argument, so a column is `valueText`, `principalId`,
   `expiresAt` — the SQL name stays snake_case and only the C++ side is camelCase.
3. `sqlpp::current_timestamp` is not comparable to a column, so "now" is a bound parameter. That is
   also the better contract: the clock becomes an explicit input a test can pin.

Two decisions live in the statement rather than in its caller. The snapshot query reads `tier` instead
of filtering it, because the process holding the snapshot decides which tiers it may carry and a
`secret` value must be dropped before it is ever built in. The session query carries `revokedAt IS
NULL` and the expiry comparison, so a revoked session cannot be resurrected by a forgotten check.

The execution side, `core/db/` implementing sqlpp23's connection interface over Drogon's async pool, is
not written: it needs Drogon's headers, and `vcpkg install` has not run.

## Why the two definer functions exist

`settings_write_audit()` and `settings_bump_version()` run with the owner's rights, because the
application must write a setting without being able to write its audit row or set the version counter.
Granting either of those to the application would mean a bug in the service layer could forge history
or force every listener to reload. Both are revoked from `PUBLIC` and reachable only through triggers.
