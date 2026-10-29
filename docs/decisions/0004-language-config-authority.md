# 0004 — Language matrix, configuration as data, and the authority model

Status: accepted · 2026-10-28

Decided with the operator over three rounds of comparison. Each section records the shape chosen, why
it wins, and what was rejected, because a later reader cannot reconstruct a decision from a diff.
Section 1 keeps 0001 §2's judge core and amends that section's service language.

## 1. One language per kind of work

Chosen: **C++23 is the default for every component.** A part leaves C++ only by naming the thing C++
does not have; it goes to Rust next, and to TypeScript only where the browser owns the package. The
rule is checkable in one line, which is the point — "this part is hard" is not a reason to change
language, and a named missing package is.

**C++ owns the api and the web layer.** Drogon 1.9.14 (published 2026-10-12) serves both, with views
rendered by Inja and partial updates carried by a vendored htmx of about 14 KB. Data access goes
through `drogon::orm::DbClient::newPgClient`: an async Postgres pool with prepared statements on the
same event loop as the request. Rejected: libpqxx 8.0.2, which is a valuable library but speaks
blocking libpq, so every query would need a hop onto a worker thread and the pool would stop being
the request's pool. Sessions come from `drogon::plugin::Session`; nothing about the account model is
reimplemented.

**C++ owns the judge.** The supervisor reaps children, tears down the cgroup, escalates to `SIGKILL`
and enforces the output cap; the checkers are C++ on testlib 0.9.41; the sandbox is isolate v2.7. The
case for Rust here was memory safety under adversarial cancellation, and on a current kernel that case
does not survive contact with the mechanism: `pidfd_open` and `pidfd_send_signal` (Linux 5.1 and 5.3)
replace the `waitpid`/`kill` pair that made the pid-reuse race possible, the cgroup descriptor is held
for the run's lifetime, and every resource is released by a destructor. The supervisor reads no
untrusted structured data — it counts bytes and signals a process — so the parser surface that would
justify a second language never appears. The second argument is size: the judge image must already
ship a C++ compiler to build submissions, so a Rust supervisor adds a second toolchain and a second
lockfile inside the one container that executes hostile code.

**Rust owns `cmsctl`, the full-permission administrative CLI**, as a crate with no database
dependency. It is Rust for a structural reason rather than an ergonomic one: two languages cannot
share symbols, so the CLI is physically unable to call a C++ internal function. "Administrative
actions must pass through the core" stops being a review rule and becomes a linker error.

**TypeScript owns exactly two things.** The first is `tools/validate-content.ts`, already shipped with
eleven falsification cases; rewriting working CI code to satisfy a language preference is waste. The
second is the CodeMirror 6 editor island, because no non-JavaScript package solves a text editor in a
browser.

Rejected for the web layer, in the order they were considered:

- **Wt 4.15.0** (4.14.4 proven). A genuine C++ web toolkit, and `Wt::Auth::AuthWidget` would have
  handed us registration, verification and approval forms as vendor code. Rejected anyway: every
  interaction is a server round-trip, each session keeps a widget tree resident, and its ownership
  model is `new Widget(parent)` raw pointers — the one island of manual lifetime in an otherwise RAII
  codebase, in the layer with the most code.
- **Leptos 0.8.22** (with Maud 0.27 and htmx as the no-WASM fallback). Its decisive advantage — one
  type definition shared by api and web, enforced by the compiler — requires the api to be Rust too.
  With a C++ core that advantage is gone, and what remains is a wasm toolchain and a second language.
- **Next 16.4.3.** The best-documented path, and the only one that puts a Node runtime and a second
  type system on the public path.

**Judge binaries.** Checkers run as separate executables under isolate; the supervisor links the same
shared verifier the judge runs, so the code that proves a task is verified is the code that runs when
it is graded. ASan, UBSan and libFuzzer stay on the checker path, and the supervisor inherits the same
build: a fork bomb and an output flood are falsification cases in the judge tests, because the failure
this design accepts is a bug in the supervisor itself rather than a race it cannot see.

## 2. Configuration is data

Chosen: **a value lives in Postgres unless it is needed to reach Postgres.** A TOML file on the
server means every policy change costs an SSH session and a restart, which is a permanent tax paid by
whoever runs the box; the requirement is that a teacher can change how the site behaves without being
that person.

Four layers, and nothing else:

1. **Host file**, read-only, `0600`, written once at provision: `CMSLEARN_DATABASE_URL`,
   `CMSLEARN_SEAL_KEY`, `CMSLEARN_BOOTSTRAP_ADMIN`, TLS mode. These four facts let a process find and
   trust the database. Storing the connection string in the database is circular, and the key that
   unseals secrets must not sit beside the ciphertext.
2. **`settings` rows**: policy, limits, rate limits, routing, image digests, feature flags, sealed
   credentials.
3. **Content tables**: the task bank, seeded idempotently from the image by content hash, then live.
4. **Code**: capability names, and each setting's schema — type, range or enum, default, tier, and
   whether it is secret. These are deliberately not data (see §3).

**Hot reload, in one pass.** A statement-level trigger on `settings` bumps `settings_meta.version` and
calls `pg_notify` with that version; the Postgres manual recommends a trigger for exactly this so the
application cannot forget to notify. Each process holds
`std::atomic<std::shared_ptr<const SettingsSnapshot>>`; the hot path is one atomic load, no lock and
no query. On notification, a process reads `settings`, builds a snapshot and swaps the pointer; a
notification whose version is at or below the current one is ignored, which makes duplicate and
reordered deliveries harmless. Every 30 seconds one `SELECT version FROM settings_meta` runs as a
fallback, because a notification is delivered only to a *connected* listener: a process whose
connection dropped never receives what it missed, and would otherwise serve stale policy until
restarted. That fallback is not redundant with the trigger; it is the answer to the failure mode that
makes a naive NOTIFY-only design silently wrong.

**Privileged changes are two-phase**, the same shape as a migration: a plan returns the diff and the
blast radius, and an apply takes the plan id. One mental model covers schema changes and
configuration changes, and a change that needs a restart says so before it is applied.

Where the fact cannot be hot-reloaded, it is still made admin-editable:

- **Routing, headers, rate limits** — render the Caddyfile from the rows, `POST /adapt` to validate
  without running anything, then `POST /load`, which the Caddy documentation states blocks until the
  reload succeeds or fails and incurs zero downtime, rolling the previous configuration back on
  failure.
- **Image versions** — `release.toml` digests, `docker compose pull`, `up -d --wait` per service; the
  rollback is re-applying the previous digest row.
- **Schema** — `schema.hcl` to `atlas migrate diff`, then `lint`, then one-shot apply (§4).
- **DSN, listen port, seal key** — never. Changing them is a host operation by definition, and
  pretending otherwise would be the circularity in layer 1.

## 3. Authority: `system:root` is a non-login principal

Chosen: one full-permission principal that cannot be logged in as, acting only through the same core
code as every other actor, with human attribution on every row it writes.

**Capabilities are an enumerated bitmask.** Capability names are compile-time constants and the
role-to-capability mapping is data, so editing a row can grant an existing capability but can never
mint a new one. A wildcard is banned: "root can do anything" is the one claim an auditor cannot
verify, while an enumerated set is diffable. The check is one `AND` against the snapshot the request
already holds — no query, no string comparison, no branch on a role name.

**Root cannot log in, enforced three ways**: system principals carry no password hash, so there is
nothing to authenticate against; the login query filters on `kind = 'user'`; and a database `CHECK`
plus a trigger rejects a session row for a system principal, so a future code path that forgets the
filter still cannot create one. `/admin/*` exists only on `/run/cmslearn/admin.sock`, guarded by
`SO_PEERCRED`; it is never on the public listener and Caddy never proxies it. `cmsctl` holds no
`DATABASE_URL`, because the moment it can open a psql connection the core has no authority.

**Every audit row records the actor and `invoked_by`** — the host key fingerprint of the sealed
credential, or the elevation ticket. A principal named root with no human behind it answers nothing,
and the `system:` prefix keeps log queries from conflating it with OS uid 0. The actor is never read
from a request body or header; it is derived from the authenticated principal.

**Elevation is time-boxed**, and irreversible operations — dropping content, rotating the seal key,
changing an image digest, applying a migration — require a second approver. A stolen credential then
buys minutes of one capability rather than a permanent skeleton key.

**Bootstrap is an identity, not a branch.** Inside one transaction the seed inserts a `bootstrap`
principal, creates the first human admin and `system:root`, zeroes the bootstrap capabilities, writes
a `bootstrap.completed` audit row and commits. There is no post-commit state in which bootstrap
exists, it writes audit rows like any other actor, and no `if (database_empty)` code path is created
that later has to be remembered and removed.

`settings_audit` is INSERT and SELECT only for the application role, with `REVOKE UPDATE, DELETE`, and
the audit row is written in the same transaction as the mutation — so a change that exists without its
row is impossible by construction rather than by discipline.

## 4. Schema ownership

Chosen: `deploy/schema.hcl` is the only DDL in the repository. `atlas migrate diff` writes the SQL,
`atlas.sum` proves the SQL was not hand-edited, and `atlas migrate lint` refuses destructive changes
before they are applied. Hand-written migration files are the failure class this removes.

## 5. Consequences for files that exist today

- `REGISTRATION_OPEN`, `APPROVAL_REQUIRED`, `ALLOWED_EMAIL_DOMAINS` and `SESSION_TTL_MINUTES` stop
  being environment variables (`deploy/docker-compose.yml:61-77`) and become settings rows.
- `web/` stops being a Next.js tree and becomes a C++ target. `web/tokens.css` and
  `web/public/fonts/` stay exactly as they are — they are the theme, not the framework.
- `content/` keeps its file shape as the authoring format and the generation target; it becomes the
  seed rather than the live source, so the content gate must run against the database as well as
  against files.
- Three toolchains are pinned instead of one, and two languages appear in the admin path.

## 6. Gates this decision creates

- `check:migrations` — `atlas.sum` intact and `atlas migrate lint` clean.
- `check:stack` — the rendered compose file equals the committed one.
- `check:settings` — every key read by code exists in the settings schema, and every default validates
  against its own declaration.

Each ships with a falsification case that plants the defect and proves the gate fails, matching the
convention set by `tools/falsify-*.sh`.
