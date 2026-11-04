# Build order

Epics are deliverables. Steps are one commit each. Subsections are structure inside a step and never
commit on their own. Nothing above a step is dispatched to anyone.

Every step ships a gate and a falsification case; a step with neither is not done.

## E1 — one schema source, one stack, one upgrade command

**Deliverable:** the schema, the container stack and the running version each derive from one file,
and an operator upgrades with one command.

| Step | Work | Gate | Falsification |
|---|---|---|---|
| E1.1 | ADR 0004 and this plan | review | — |
| E1.2 | `deploy/schema/` — authority, audit, settings (content tables follow in E4) — linked by `main.sql`, rendered by `pgschema plan` into `deploy/plan/0001_init.sql` + `.json`, executed by `tools/apply-plan.sh` | `check:schema` | hand-edit a rendered plan, or edit it and reseal `CHECKSUMS`; the wrapper and the gate must refuse it |
| E1.3 | `release.toml` (version, per-image digest) and `tools/render-stack.ts` rendering `deploy/docker-compose.yml` | `check:stack` | change one digest; the committed compose must stop matching |
| E1.4 | `make upgrade`: pull by digest, one-shot `schema` container running `pgschema apply --plan`, `up -d --wait`, `/healthz` smoke, previous release kept for rollback | `check:upgrade` (dry run) | plant a failing healthcheck; the command must stop before switching |
| E1.5 | `tools/render-plan.sh` renders `deploy/plan/0001_init.{sql,json}` and `CHECKSUMS` from `deploy/schema/main.sql` through the same `pgschema plan` call the gate makes; the plan is generated, never hand-tended | `check:schema` — a fresh render is compared with the committed plan, and apply leaves no residual change | append a line to a rendered plan (and reseal `CHECKSUMS`); the fresh render must still catch it |

## E2 — api skeleton

**Deliverable:** a service that starts, reports health, serves metrics, and exposes the admin socket.

| Step | Work | Gate | Falsification |
|---|---|---|---|
| E2.0 | `db/`: sqlpp23 table headers rendered by `tools/render-tables.sh` from `information_schema.columns` on a scratch cluster, and a CRTP connector over `drogon::orm::DbClient` — `_select` and `_execute` only, so insert/update/prepare stay compile-time absences until E3 needs them | `check:db` — the committed header equals what the schema renders, compared table by table and column by column | delete a column struct; the gate names the column and the line. Shift the placeholder numbering in `context::bind`; the serialization test names the wrong position |
| E2.1 | CMake workspace, `api/` target, Drogon app reading only the host file, multi-stage `api/Dockerfile` | `cmake --build` clean under `-Wall -Wextra -Werror` | remove a required host fact; startup must fail loudly rather than default |
| E2.2 | settings snapshot service (atomic pointer, NOTIFY listener, 30 s version check), `/healthz`, `/metrics` | `check:settings` | declare a setting in code that is absent from `deploy/schema/` and prove the gate fails |
| E2.3 | the admin channel: a Unix socket speaking one JSON object per line, mode 0600, every accept checked with `SO_PEERCRED`; Drogon serves TCP only, so the listener is ours | `api_settings_e2e` asserts the socket is bound, is 0600, answers `ping`, and that the public listener carries no admin route | send an admin request to the public listener; it must 404 |
| E2.4 | `tools/check-live.sh` starts a scratch cluster, plans one template database, gives each `[live]` binary a copy, exports `CMSLEAN_TEST_DSN`, runs exactly the cases tagged `[live]`, and tears the cluster down | `check:live` — the tag selects cases, every selected case runs, and selecting nothing fails | a `[live]`-tagged defect that the tag no longer selects; the gate must report it rather than pass on an empty selection |

What E2.0 does **not** prove, stated so it is not assumed later:

- `sqlpp23` has no tag for `uuid`, `timestamptz`, `inet`, `jsonb` or any of the six enum types, so those
  columns are carried as `text` or `timestamp`. A passing round trip proves the text survived, not that
  the type is faithful.
- The round trip writes and reads values. It does not exercise a `CHECK`, a foreign key, a partial index,
  a trigger, row-level security or the grant matrix.
- The connector is synchronous because sqlpp23's `_select` is. Whether a caller on an event-loop thread
  must hop to a worker is E2.2's concern, and nothing here measures it.
- `aws-sdk-cpp`, `inja`, `spdlog`, `libsodium` and `simdjson` are declared dependencies that no source
  file includes yet.

## E3 — identity and authority

**Deliverable:** accounts reach `active` through the documented state machine, and `cmsctl` acts only
through the core.

| Step | Work | Gate | Falsification |
|---|---|---|---|
| E3.1 | signup, domain allowlist, verification, approval, sessions, audit rows in the same transaction | auth integration tests | attempt a session for a system principal; the database must reject it |
| E3.2 | capability bitmask and `authorize()` in the service layer, no wildcard | capability tests | grant a capability name that does not exist in code; it must be denied |
| E3.3 | `cmsctl`: plan, apply, time-boxed elevation, two-person for irreversible operations, no database dependency | CLI contract tests | add `sqlx` to the crate; the build gate must fail on the dependency rule |
| E3.4 | bootstrap as one transaction, `bootstrap.completed` audit row, no empty-database branch | seed test on an empty database | re-run the seed on a populated database; it must refuse |

## E4 — content and serving

**Deliverable:** the bank lives in Postgres, is seeded from the image, and a student is served an
unseen task by one indexed query.

| Step | Work | Gate | Falsification |
|---|---|---|---|
| E4.1 | content tables, idempotent seeder keyed by content hash, content gate runnable against the database | `check:content:db` | seed twice; no duplicate rows and no version bump |
| E4.2 | curriculum read as data, with no branch on a track name | curriculum tests | add a third track to the data; no code change needed |
| E4.3 | unseen-task query with a stage-topic set and one band widening; index on `progress(account_id, task_id, task_version)` | query-plan assertion | drop the index; the gate must report a sequential scan |
| E4.4 | task visibility as one encoded principal column (`task_visibility`, ADR 0010): the serving query filters by the request's audience, a task with no row is served only with `content:read`, and a learner-created task carries the everyone row | `check:visibility` — the real serving query hides a task visible to nobody and serves a granted one | drop the visibility predicate from `bank/src/next_task.cpp`; the gate must serve the hidden task, i.e. go red |

## E5 — judge

**Deliverable:** a submission becomes a verdict, under limits, with no network.

| Step | Work | Gate | Falsification |
|---|---|---|---|
| E5.1 | testlib 0.9.41 checkers and the isolate v2.7 invocation contract | checker contract tests | feed a checker a malformed verdict; it must report failure, not pass |
| E5.2 | C++ supervisor: spawn, limits, output caps, `SIGKILL` escalation, cgroup teardown | supervisor tests under a fork bomb | remove the escalation timer; the fork-bomb test must hang or leak a cgroup |
| E5.3 | shared verifier writing `tests/manifest.json`, used by both CI and the judge | `check:verify` + falsify | swap in a wrong reference; verification must fail |
| E5.4 | judge run path: claim a submission (lease), compile, run each case through `run_sandboxed`, read the checker's verdict, write one `submissions` row | `check:judge` — a claimed submission is compiled, run, checked and written back as one verdict | a wrong answer that is written as `accepted`; the gate must catch it |

## E6 — web

**Deliverable:** a student and an admin can both use the site with no Node process in production.

| Step | Work | Gate | Falsification |
|---|---|---|---|
| E6.1 | Inja layout, TH/EN, `tokens.css` mapped through the Tailwind theme layer | render snapshot tests | remove a TH label from content; the page must fall back, not render empty |
| E6.2 | task view with the CodeMirror island, submit, verdict polling | end-to-end test against a real submission | disable the island; the form must still submit a plain textarea |
| E6.3 | schema-generated settings forms with plan and apply, tier enforced in the UI and in the service | admin form tests | render a `secret` setting; no read-back path may expose the value |
| E6.4 | authenticated task page and verdict fragment: an approved session is required (no session or an unapproved account is 303 to `/signin`), `content:read` reveals a hidden task, the published version is the one the submission write targets, unknown/draft/hidden share one 404, and `/tasks/{id}/verdict` answers the polling fragment | `api_task_route_e2e` (plus the task-page and verdict-fragment unit cases) | request a task with no session; it must redirect, and a hidden task must 404 without `content:read` — a 200 for either is the defect |
| E6.5 | editor island: `web/island/editor.ts` is bundled by `tools/build-island.ts` into `web/public/island.js` with its licence, the page loads it outside the form, and the textarea posts when it never loads | `check:island` — the fallback markup is intact and the committed bundle is what the source builds | edit the island source without rebuilding; the gate must refuse the stale bundle |

## E7 — generation pipeline

**Deliverable:** a draft becomes published only through machine evidence.

| Step | Work | Gate | Falsification |
|---|---|---|---|
| E7.1 | draft generation writing the same file shape the seeder consumes | generation tests | generate a task with a missing field; the content gate must reject it |
| E7.2 | promotion: verify, then publish, artifact stored content-addressed | `check:publish` | attempt publish with pending verification; it must be refused |

## E8 — continuous integration

**Deliverable:** every gate and every falsification runs on each push, in a job that can fail.

| Step | Work | Gate | Falsification |
|---|---|---|---|
| E8.1 | `.github/workflows/ci.yml`: `content`, `references`, `deploy-contract`, `security`, `gates`, `cpp-fast` and `cpp-full` jobs; `cpp-full` also runs `check:live`, `check:judge`, `check:visibility`, `check:db`, `check:unseen` and their falsifiers | none yet — CI was verified by parsing the file with `yq` and resolving every `bun run` it names to a `package.json` script; one name (`check:render`) does not resolve and is reported rather than claimed | — no falsifier: a gate removed from a job is not detected by any gate today |

## Decisions that shaped this plan

| Decision | Chosen | Evidence |
|---|---|---|
| Schema generator | pgschema 1.13.4, Apache-2.0, pinned binary verified by SHA-256 | `.agents/memos/2026-10-29-decisions.md` |
| Plan database | a second database in the same cluster, named by `--plan-*` | no embedded Postgres download at runtime |
| Frontend | server-rendered HTML: Inja over Drogon, htmx for fragments | MDN: WebAssembly cannot reach the DOM; five C++ candidates measured |
| CSS | Tailwind 4.3.3 + daisyUI 5.7.50, build time only | generated output only carries used classes |

## Standing rules

- One commit per step, `WIP:` until the step is finished; the finishing commit drops the prefix.
- No host-side scripting and no host environment variable beyond the four bootstrap facts. Production
  is containers, pinned by digest.
- Every gate has a falsification case in `tools/falsify/cases/*.ts`.
- A step delivered in a different shape than agreed is a broken finding, reported as such.
