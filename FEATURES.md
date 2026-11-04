# FEATURES.md — cms-learn

Every feature the project promises, with its real state and the artifact that proves it.

**Legend:** ● shipped · ◐ partial (code exists, gate or coverage missing) · ○ planned (decided, no
code)

**Class:** `A` — belongs in the proposal, because a reader's decision or a promise to students
depends on it. `C` — internal engineering, deployment or tooling detail; real, but not something the
proposal argues. `—` — not yet classified.

**Rule for this file:** a row moves only when the evidence moves. Adding a feature here is a promise
an agent can be held to; marking one ● without evidence is the failure this file exists to prevent.

IDs are allocated by area and never reused, so gaps are reserved, not lost: `F-001–019` content,
`F-020–029` judge, `F-030–049` web, `F-050–059` accounts and security, `F-060–079` operations.

Counts at last update: **36 shipped · 7 partial · 12 planned** (55 features).

---

## Content and curriculum

| ID | Feature | Class | State | Evidence |
|---|---|---|---|---|
| F-001 | Controlled topic vocabulary, TH+EN labels (33 topics) | A | ● | `content/topics.json` |
| F-002 | Routes are stages of tags with a difficulty band; there is no module layer | C | ● | `content/tracks.json` |
| F-003 | Two routes as parallel views of one bank — USACO ladder and IOI path — 4 stages each, every tag placed in both, difficulty shown as a number | C | ● | `content/tracks.json` |
| F-004 | Interface in Thai and English; a record made from the panel carries an optional second language, at least one language required, none privileged | A | ● | `content/schema/tracks.schema.json`, `content/tracks.json` |
| F-005 | Stage lists validated: unknown tag fails, tag twice in one stage fails, unplaced tag fails | C | ● | `tools/validate-content.ts` (cases 4, 5, 9) |
| F-006 | Task schema: topics, difficulty, limits, checker, origin, verification | A | ● | `content/schema/task.schema.json` |
| F-007 | Reachability rule — a task no stage can serve fails the gate | C | ● | `tools/validate-content.ts` (case 11) |
| F-008 | Publish gate — `published` refused without 3 verification methods and test data | A | ● | `tools/validate-content.ts` (case 2) |
| F-009 | Seed tasks with bilingual statements, machine-checked examples and compiling references | A | ● | `content/tasks/*/examples/`, `tools/check-references.sh` |
| F-010 | Test data with manifests per task | A | ● | `content/tasks/*/tests/manifest.json` (one per task), `tools/check-verify.sh` |
| F-011 | Task generation from a setter's brief: a complete draft in the shape the seeder reads, refused when a required field is missing | A | ● | `judge/src/generate.cpp` (refuses an incomplete spec), `tools/check-generator.sh` (the loader reads the draft; two runs are identical) |
| F-012 | Brute-force differential verifier | A | ○ | spec: `docs/content-model.md` |
| F-013 | Limit measurement (time/memory with margin) | A | ○ | spec: `docs/content-model.md` |
| F-014 | Per-account unseen-task serving (topic × difficulty × band, audience-filtered) | C | ● | `bank/src/next_task.cpp` (audience set, topic set, one band widening), `tools/check-unseen-task.sh` |
| F-015 | Copy/plagiarism detection between classmates | C | ○ | — |
| F-016 | Task visibility: one encoded principal column; a task with no visibility row is hidden and `content:read` reveals it (ADR 0010) | A | ● | `bank/include/cms/bank/visibility.hpp`, `docs/decisions/0010-task-visibility.md`, `tools/check-visibility.sh` + `tools/falsify/cases/visibility.ts` |

## Judge

| ID | Feature | Class | State | Evidence |
|---|---|---|---|---|
| F-020 | `isolate` sandbox in a container with delegated cgroup v2 | A | ● | `judge/isolation/Dockerfile`, `deploy/docker-compose.yml` (`cgroup: host`, internal network with no gateway); the fork-bomb case `judge/tests/supervisor_test.cpp` skips on a host without `isolate` |
| F-021 | C++ judge core with `testlib` checkers | A | ● | `judge/checkers/token.cpp`, vendored testlib 0.9.41 at `judge/third_party/testlib.h` with its licence and digest in `judge/third_party/README.md` |
| F-022 | Language runtimes: C++ only, one baseline standard (`c++17`); a run can be pointed at another with `--standard`, but a task has no per-task choice | A | ◐ | `judge/include/cms/judge/run.hpp` (`DEFAULT_CPP_STANDARD`), `judge/src/run.cpp`; several standard versions are not shipped |
| F-023 | Interactive problem protocol: the pipeline generates a testlib interactor per task | C | ○ | task schema has `checker.kind: interactive`; testlib documents `registerInteraction` |
| F-024 | Submission queue with per-submission status | A | ● | `judge/src/worker.cpp` (claim + lease), `judge/src/run.cpp` (compile → run → check → one verdict), `tools/check-judge.sh`; the status is `submissions.verdict`/`judged_at` |
| F-025 | Abuse controls: egress disabled, fork bomb, output cap, wall-clock kill | A | ● | `judge/src/supervisor.cpp` (memory/process limits, `CAPTURE_BYTES`, SIGTERM→SIGKILL escalation), `deploy/docker-compose.yml` judge on the gateway-less `internal` network (asserted by `tools/check-deploy.sh`), fork-bomb case in `judge/tests/supervisor_test.cpp` |
| F-026 | Function-style submission: a generated harness wraps the contestant's function, chosen per task | C | ○ | — |

## Web

| ID | Feature | Class | State | Evidence |
|---|---|---|---|---|
| F-030 | Design tokens, `judge mono` (contrast 15.41:1 body) | C | ● | `web/tokens.css` |
| F-031 | Thai-coverage monospace font, shipped with licence, verified by advance-width measurement | C | ● | `web/public/fonts/` |
| F-034 | Drogon + Inja + htmx app shell reading the tokens | C | ● | `api/src/web.cpp` (`render_page`, Inja with autoescape), `api/templates/*.html`, `web/public/htmx.min.js`, `api/src/page_routes.cpp` |
| F-035 | Path home: stage rail, current module, next task | C | ○ | layout decided in ADR 0002 |
| F-036 | Task view: title, limits, statement text, first sample pair | A | ● | `api/src/page_routes.cpp` (`register_task_route`), `api/src/task_view.cpp`, `api/templates/task.html` |
| F-037 | Code entry: in-page CodeMirror editor that writes back into the plain textarea the form posts | A | ● | `web/island/editor.ts` → `web/public/island.js`, `design/pages/task.tsx`; fallback asserted by `tools/check-island-fallback.sh` (and its falsifier) |
| F-038 | Submission result: verdict, per-test cost, diff of first failure | A | ◐ | verdict and 2 s polling: `api/src/verdict_routes.cpp` + `design/pages/task.tsx` (`hx-get`); per-test cost and the first-failure diff do not exist |
| F-039 | Progress and weak-topic view | C | ○ | layout decided in ADR 0002 |
| F-040 | Dictionary wiring for TH/EN across every surface | A | ● | `api/src/web.cpp` (`choose_locale`, `label`, one-word fallback), reused by `api/src/auth_view.cpp` and `api/src/settings_page.cpp`; fallback case in `api/tests/web_test.cpp` |

## Accounts and security

| ID | Feature | Class | State | Evidence |
|---|---|---|---|---|
| F-050 | Self-signup, open to anyone, gated by admin approval before the account becomes active | A | ● | `api/src/auth_routes.cpp` (`/signup`, `/verify`, `/signin`, `/signout`), `api/src/accounts.cpp` (`sign_up`, `verify_email`, `create_session` refuses a non-active account), `api/tests/auth_e2e.sh` |
| F-051 | Organisation recorded at signup; an account outside a known organisation is marked external rather than refused | A | ○ | ADR 0002 §2 |
| F-052 | Admin approval before an account becomes active | A | ◐ | the rule is enforced: `api/src/accounts.cpp` (`approve_account` requires a verified address; `create_session` refuses a non-active one); the missing half is an operator surface — the admin channel exposes only ping/status/settings/elevation, so no route calls `approve_account` yet |
| F-053 | Account state machine recorded in an audit trail | A | ● | `api/src/audit.cpp` + `api/src/accounts.cpp` (`account.signup`, `account.verify`, `account.approve`, `session.create` rows), asserted by `api/tests/auth_e2e.sh` |
| F-054 | Rate limiting and lockout | A | ◐ | edge limits shipped in `deploy/Caddyfile` (auth 20/min, submissions 60/min per IP); per-account lockout is app-side and not built |
| F-055 | PII minimisation and retention policy (minors) | A | ○ | ADR 0002 §2 |

## Operations

| ID | Feature | Class | State | Evidence |
|---|---|---|---|---|
| F-060 | CI running every gate and every falsification on each push | C | ◐ | `.github/workflows/ci.yml` — jobs `content`, `references`, `deploy-contract`, `security`, `gates`, `cpp-fast`, `cpp-full`; `cpp-full` runs `check:live`, `check:judge`, `check:visibility`, `check:db`, `check:unseen` and their falsifiers. Missing half: `cpp-full` also names `bun run check:render`, which has no `package.json` script (`bun` exits 1: "Script not found"), so the job is red until that name exists |
| F-061 | Containerised stack (compose), no host installs | C | ● | `deploy/docker-compose.yml` — 10 services, contract asserted by `tools/check-deploy.sh` |
| F-062 | Ingress and TLS | C | ● | `deploy/Caddyfile` + `deploy/proxy/Dockerfile` — Caddy 2.11.7, auto-ACME, HSTS, per-IP rate limits; `caddy validate` run inside the built image |
| F-063 | Backups with a restore drill | A | ● | `deploy/backup/run.sh`, `restore-drill.sh` — verified end to end against Postgres 18.6 (250 users / 1000 submissions restored, live DB untouched) |
| F-064 | Observability: judge queue depth, submission latency, error rate | C | ◐ | `/metrics` exists on the api only (`api/src/main.cpp`); the judge has none, and queue depth/latency/error-rate are not exposed |
| F-065 | Image and dependency CVE scanning | C | ● | `tools/security-scan.sh` — Trivy 0.66.0 for secrets, fixable CVEs and deployment hardening; `tools/falsify/cases/security.ts` catches 3 planted defects and keeps a clean file green |
| F-066 | Judge host provisioned operator-manual (KVM not required with `isolate`) | C | ○ | ADR 0001 §1 |
| F-067 | Discord failure notification from CI (secret set by the operator) | C | ◐ | `.github/workflows/ci.yml` — wired and exercised via `simulate_failure`, `DISCORD_WEBHOOK_URL` not yet set |
| F-068 | Deploy job, gated on a tag rather than on every push | C | ○ | needs the three service images; CI reports "no target yet" |
| F-069 | Single-server exposure contract: only the proxy is public, db/dashboards loopback, judge has no egress | A | ● | `tools/check-deploy.sh` + `tools/falsify/cases/deploy.ts` — 7/7 broken stacks rejected |
| F-070 | WAF + IP reputation at the edge, CRS evaluated **in-band** so the attack itself gets 403 | C | ● | `deploy/crowdsec/` — verified `Loaded 1 inband rules`, `Appsec listening on 0.0.0.0:7422` |
| F-071 | Encrypted offsite backups whose private key never touches the server | A | ● | `deploy/backup/run.sh` — `age` recipient only; refuses to run without a public key |
| F-072 | Registration and approval as runtime switches (no email dependency) | C | ● | `deploy/catalogue/settings.json` (`site.registration_open`, `site.registration_requires_approval`), read by `api/src/accounts.cpp`; live case "a closed registration refuses everyone" in `api/tests/accounts_test.cpp` |
| F-073 | Every C++ unit built by CMake presets under ASan + UBSan with `-Werror`, run through ctest, and analysed by clang-tidy — with the gate proven to fail | C | ● | `CMakePresets.json`, `cmake/Sanitizers.cmake`, `CMakeLists.txt` + `tools/cpp/gate.ts` (42 targets, 88 cases, 81 units analysed, 0 findings on the last run) + `tools/cpp/falsify.ts` **7/7**: use-after-free, signed overflow, stack overflow, `-Wunused-variable`, `bugprone-use-after-move`, a clean control and an empty tree — mandatory because ADR 0006 puts C++ on the internet-facing path |
| F-074 | Admin surface on its own listener and container, with its own session scope | C | ○ | ADR 0006 §3 — the compose contract still describes 10 services; this is the one that will change the count |
| F-075 | One plan render driver: `deploy/plan` is output of `deploy/schema/main.sql`, and the gate renders it again rather than trusting the commit | C | ● | `tools/render-plan.sh`, `tools/check-schema.sh`, `tools/falsify/cases/schema.ts` |
| F-076 | One falsification harness for the gates: each case is data, staged in a tree, and a mutation that does not land reads as setup failure | C | ● | `tools/falsify/runner.ts`, `tools/falsify/cases.ts`, `tools/falsify/self-check.ts` (`bun run falsify:runner-selfcheck`) |

---

## How to update this file

1. Find the row (or add one) — one row per user-visible capability, not per file.
2. Move the state only with evidence: a commit that landed the code **and** the gate that proves it.
3. `◐` means exactly what it says: code exists, proof does not. Do not round it up to ●.
4. Counts at the top are re-counted, not adjusted by hand.
