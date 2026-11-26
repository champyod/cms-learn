# FEATURES.md — cms-learn

Every feature the project promises, with its real state and the artifact that proves it.

**Legend:** ● shipped · ◐ partial (code exists, gate or coverage missing) · ○ planned (decided, no
code; the row says what would start it, so a reader can tell a plan from a wish)

**Class:** `A` — belongs in the proposal, because a reader's decision or a promise to students
depends on it. `C` — internal engineering, deployment or tooling detail; real, but not something the
proposal argues. `—` — not yet classified.

**Rule for this file:** a row moves only when the evidence moves. Adding a feature here is a promise
an agent can be held to; marking one ● without evidence is the failure this file exists to prevent.

IDs are allocated by area and never reused, so gaps are reserved, not lost: `F-001–019` content,
`F-020–029` judge, `F-030–049` web, `F-050–059` accounts and security, `F-060–079` operations.

Counts at last update: **53 shipped · 6 partial · 8 planned** (67 features).

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
| F-008 | Publish gate — `published` needs a verified certificate for the task and version: a row cannot be created published, a move to published needs the certificate, the seeder refuses a published file without one and names the command that earns it, and serving requires `verification = verified` | A | ● | `deploy/schema/content.sql` (`tasks_publish_needs_certificate`), `bank/src/seed.cpp` (`refuse_published`), `api/src/task_view.cpp` (`servable_task`), `tools/validate-content.ts` (a file may not claim its own verification, ADR 0046 §2); live cases in `generator/tests/certify_test.cpp`, `api/tests/task_route_e2e.sh`, `tools/check-visibility.sh` |
| F-009 | Seed tasks with bilingual statements, machine-checked examples and compiling references | A | ● | `content/tasks/*/examples/`, `tools/check-references.sh` |
| F-010 | Test data with manifests per task | A | ● | `content/tasks/*/tests/manifest.json` (one per task), `tools/check-verify.sh` |
| F-011 | Task generation from a setter's brief: a complete draft in the shape the seeder reads, refused when a required field is missing | A | ● | `judge/src/generate.cpp` (refuses an incomplete spec), `tools/check-generator.sh` (the loader reads the draft; two runs are identical) |
| F-012 | Brute-force differential verifier | A | ● | `generator/src/solution_proof.cpp` (`check_solutions` compares through `cms::tokens_equal`, ADR 0042), `core/include/cms/tokens.hpp` (the one token rule, cross-checked against the judge's checker inside the gate), `generator/src/sampler.cpp` + the typed subtask constraint (the sampled inputs follow the subtask's own kind); live cases in `generator/tests/solution_test.cpp` |
| F-013 | Limit measurement (time/memory with margin) | A | ● | `generator/src/oracle.cpp` (`measure_program` samples the program's own peak resident memory after exec, so a forking parent cannot inflate it), `generator/src/certify_checks.cpp` (the reference must stay at or below half the declared time and memory, ADR 0048); refusals named by file and both halves in `generator/tests/certify_test.cpp` |
| F-014 | Per-account unseen-task serving (topic × difficulty × band, audience-filtered) | C | ● | `bank/src/next_task.cpp` (audience set, topic set, one band widening), `tools/check-unseen-task.sh` |
| F-015 | Copy/plagiarism detection between classmates | C | ○ | not started: no similarity method chosen, and nothing reads two submissions together |
| F-016 | Task visibility: one encoded principal column; a task with no visibility row is hidden and `content:read` reveals it (ADR 0010) | A | ● | `bank/include/cms/bank/visibility.hpp`, `docs/decisions/0010-task-visibility.md`, `tools/check-visibility.sh` + `tools/falsify/cases/visibility.ts` |
| F-017 | The statement step writes a task's title and statement in each locale it ships: six fixed sections in the order ADR 0031 names, and the worked example taken from the stored brute-force program (ADR 0034) | A | ● | `generator/src/statement.cpp` (prompt, answer parse, sinks), `generator/tests/statement_test.cpp` (order, refusal by name, oracle bytes, identical runs) |
| F-018 | Two leak checks refuse a title or statement that repeats the teacher's request (eight English words, thirty-two Thai characters) or names the intended approach by id, label or acronym, reading a temporary copy the caller removes (ADR 0035, 0037) | A | ● | `tools/check-brief-leak.sh`, `tools/check-algo-leak.sh`, `generator/src/statement_leak.cpp`, `tools/falsify/cases/brief_leak.ts`, `tools/falsify/cases/algo_leak.ts` |

## Judge

| ID | Feature | Class | State | Evidence |
|---|---|---|---|---|
| F-020 | `isolate` sandbox in a container with delegated cgroup v2 | A | ● | `judge/isolation/Dockerfile`, `deploy/docker-compose.yml` (`cgroup: host`, internal network with no gateway); the fork-bomb case `judge/tests/supervisor_test.cpp` skips on a host without `isolate` |
| F-021 | C++ judge core with `testlib` checkers | A | ● | `judge/checkers/token.cpp`, vendored testlib 0.9.41 at `judge/third_party/testlib.h` with its licence and digest in `judge/third_party/README.md` |
| F-022 | Language runtimes: C++ only, one baseline standard (`c++17`); a run can be pointed at another with `--standard`, but a task has no per-task choice | A | ◐ | `judge/include/cms/judge/run.hpp` (`DEFAULT_CPP_STANDARD`), `judge/src/run.cpp`; several standard versions are not shipped |
| F-023 | Interactive problem protocol: the pipeline generates a testlib interactor per task | C | ○ | not started: the schema already reserves `checker.kind: interactive`; a task still needs its interactor produced and a supervisor handshake |
| F-024 | Submission queue with per-submission status | A | ● | `judge/src/worker.cpp` (claim + lease), `judge/src/run.cpp` (compile → run → check → one verdict), `tools/check-judge.sh`; the status is `submissions.verdict`/`judged_at` |
| F-025 | Abuse controls: egress disabled, fork bomb, output cap, wall-clock kill | A | ● | `judge/src/supervisor.cpp` (memory/process limits, `CAPTURE_BYTES`, SIGTERM→SIGKILL escalation), `deploy/docker-compose.yml` judge on the gateway-less `internal` network (asserted by `tools/check-deploy.sh`), fork-bomb case in `judge/tests/supervisor_test.cpp` |
| F-026 | Function-style submission: a generated harness wraps the contestant's function, chosen per task | C | ○ | not started: no harness field exists yet, so a task cannot declare one; it needs the artefact and the judge step that wraps a function |

## Web

| ID | Feature | Class | State | Evidence |
|---|---|---|---|---|
| F-030 | Design tokens, `judge mono` (contrast 15.41:1 body) | C | ● | `web/tokens.css` |
| F-031 | Thai-coverage monospace font, shipped with licence, verified by advance-width measurement | C | ● | `web/public/fonts/` |
| F-034 | Drogon + Inja + htmx app shell reading the tokens | C | ● | `api/src/web.cpp` (`render_page`, Inja with autoescape), `api/templates/*.html`, `web/public/htmx.min.js`, `api/src/page_routes.cpp` |
| F-035 | The curriculum page is the path home: per stage, how much the learner has done and the one task that comes next, from the bank's own selection rule | C | ● | `api/src/page_routes.cpp` + `api/include/cms/api/page_routes.hpp` (`home_page_model`, `stage_offers`), `bank/src/next_task.cpp` (`next_unseen_task_in_band`), `design/pages/home.tsx` → `api/templates/home.html`, `api/tests/web_test.cpp` |
| F-041 | Learner all-tasks page at `/tasks`: every task, several topics at once, one rung, the curriculum stage link, and a plain sentence when nothing matches | A | ● | `api/src/page_routes.cpp` (`register_task_list_route`), `api/src/main.cpp` (registration), `design/pages/tasks.tsx` → `api/templates/tasks.html` |
| F-036 | Task view: title, limits, statement text, first sample pair | A | ● | `api/src/page_routes.cpp` (`register_task_route`), `api/src/task_view.cpp`, `api/templates/task.html` |
| F-037 | Code entry: in-page CodeMirror editor that writes back into the plain textarea the form posts | A | ● | `web/island/editor.ts` → `web/public/island.js`, `design/pages/task.tsx`; fallback asserted by `tools/check-island-fallback.sh` (and its falsifier) |
| F-038 | Submission result: verdict, per-test cost, diff of first failure | A | ◐ | verdict is pushed, not polled: `deploy/schema/submissions.sql` (`submissions_verdict_notify`), `api/src/verdict_routes.cpp` (one LISTEN, fan-out to the open streams, 20 s close, two per session), `design/pages/task.tsx` (one EventSource, no interval); per-test cost, the first-failure diff and the limits shown beside the verdict do not exist yet |
| F-039 | Progress at `/progress`: per topic, attempts and solves from stored rows, with untried shown as untried and no invented threshold | C | ● | `api/src/progress_view.cpp`, `api/src/progress_routes.cpp`, `design/pages/progress.tsx`; the rows are written by the submit route and the judge (`docs/decisions/0022-progress-is-written-by-the-submit-path.md`) |
| F-040 | Dictionary wiring for TH/EN across every surface | A | ● | `api/src/web.cpp` (`choose_locale`, `label`, one-word fallback), reused by `api/src/auth_view.cpp` and `api/src/settings_page.cpp`; fallback case in `api/tests/web_test.cpp` |

### Authoring surfaces

| ID | Feature | Class | State | Evidence |
|---|---|---|---|---|
| F-032 | A task's fields are data: a task type and its fields are rows, so a new field or type needs no rebuild | A | ● | `deploy/schema/fields.sql`, `content/task_types.json`, `bank/src/seed.cpp`; `docs/decisions/0024-task-fields-are-data.md` |
| F-033 | The authoring form is generated from those rows: adding a row adds a question, deleting one removes it, and nothing outside the type's fields can be written | A | ● | `api/src/task_plan.cpp`, `api/src/admin_routes.cpp`, `design/pages/admin_task_form.tsx`, `api/tests/task_form_e2e.sh` |
| F-042 | A task's bytes live in object storage, the row holds the key, and a missing object is a refusal naming it at both doors, never empty content | C | ● | `core/src/object_store.cpp`, `api/src/admin_routes.cpp` (502 naming the key), `bank/src/seed.cpp` (`verify_objects` before any write), `tools/check-objects.sh`; the store requires a credential (`docs/decisions/0026-object-store-requires-a-key.md`) |

## Accounts and security

| ID | Feature | Class | State | Evidence |
|---|---|---|---|---|
| F-050 | Self-signup, open to anyone, gated by admin approval before the account becomes active | A | ● | `api/src/auth_routes.cpp` (`/signup`, `/verify`, `/signin`, `/signout`), `api/src/accounts.cpp` (`sign_up`, `verify_email`, `create_session` refuses a non-active account), `api/tests/auth_e2e.sh` |
| F-051 | Organisation recorded at signup; an account outside a known organisation is marked external rather than refused | A | ○ | not started: decided in ADR 0002 §2; needs an organisations table and the signup field that records one |
| F-052 | Admin approval before an account becomes active | A | ● | the rule: `api/src/accounts.cpp` (`approve_account` requires a verified address; `create_session` refuses a non-active one). The surface: `api/src/admin_actions.cpp` (`approve_learner`) reached from `cmsctl account approve` over the socket; `api/tests/admin_actions_lifecycle_test.cpp` |
| F-053 | Account state machine recorded in an audit trail | A | ● | `api/src/audit.cpp` + `api/src/accounts.cpp` (`account.signup`, `account.verify`, `account.approve`, `session.create` rows), asserted by `api/tests/auth_e2e.sh` |
| F-054 | Rate limiting and lockout | A | ◐ | edge limits shipped in `deploy/Caddyfile` (auth 20/min, submissions 60/min per IP); per-account lockout is app-side and not built |
| F-056 | One action layer both administrator surfaces call: every action checks its capability, and the acting account is passed in, never read from a request (ADR 0019) | C | ● | `api/include/cms/api/admin_actions.hpp`, `api/src/admin_actions.cpp`, `api/tests/admin_actions_creation_test.cpp` |
| F-057 | Learner, administrator and system are separate account tables, with a kind beside every recorded actor (ADR 0016, 0020) | A | ● | `deploy/schema/authority.sql`, `deploy/schema/settings.sql`, `docs/decisions/0020-one-actor-pattern.md`, `bun run check:schema` |
| F-058 | Administrator panel on its own listener: roster and five actions, per-session CSRF token compared before any work, the actor taken from the session | A | ● | `api/src/admin_routes.cpp`, `api/src/csrf.cpp`, `api/tests/admin_panel_e2e.sh` (21 assertions), `bun run check:templates` |
| F-055 | PII minimisation and retention policy (minors) | A | ○ | decided in ADR 0064: retention is fixed as the account's lifetime, with account deletion as the erasure mechanism and the schema's cascade as its enforcement; what is not built is the administrator deletion path, the notice in the settings catalogue, a case proving the cascade deletes submissions, and the store and backup side of the promise |

## Operations

| ID | Feature | Class | State | Evidence |
|---|---|---|---|---|
| F-060 | CI running every gate and every falsification on each push | C | ● | `.github/workflows/ci.yml` — jobs `content`, `references`, `deploy-contract`, `security`, `gates`, `cpp-fast`, `cpp-full`. Every script name the workflow uses exists in `package.json` (checked: 40 named, 0 missing), and `check:render` now has one |
| F-061 | Containerised stack (compose), no host installs | C | ● | `deploy/docker-compose.yml` — 10 services, contract asserted by `tools/check-deploy.sh` |
| F-062 | Ingress and TLS | C | ● | `deploy/Caddyfile` + `deploy/proxy/Dockerfile` — Caddy 2.11.7, auto-ACME, HSTS, per-IP rate limits; `caddy validate` run inside the built image |
| F-063 | Backups with a restore drill | A | ● | `deploy/backup/run.sh`, `deploy/backup/restore-drill.sh` — verified end to end against Postgres 18.6 (250 users / 1000 submissions restored, live DB untouched) |
| F-064 | Observability: judge queue depth, submission latency, error rate | C | ◐ | `/metrics` on the api now carries the queue: `cms_learn_judge_queue_depth` and `cms_learn_judge_oldest_waiting_seconds`, read in one aggregate (`api/src/submissions.cpp`, `api/src/main.cpp`); error rate and a per-request latency histogram are not built, and no case yet proves the two gauges against a database |
| F-071 | A generation screen: a job is created from a brief, and the seven steps are watched as they advance, each marked from the artefacts the worker wrote | A | ◐ | ADR 0062; proven live: 18 assertions over three cases (an account without the capability refused by name, an empty brief writing no row, a job read back queued at 0/7), and the stream's event body proven by a case without a database — `api/src/generation_page.cpp`, `api/tests/generation_action_test.cpp`, `web/public/generation.js`; what no gate covers is a browser opening the stream, so the page's live behaviour rests on the markup and the script agreeing |
| F-070 | A generated task carries a testlib validator, and the certifier proves it accepts every generated case and refuses each mutated one | A | ○ | not started: decided in ADR 0061; needs the `validator` role in `generation_programs`, a step 6 prompt, and the acceptance plus mutation set in step 7 |
| F-069 | Content policy at the edge: no inline script or style is allowed to run, and a page that breaks it is counted rather than silently allowed | C | ● | `deploy/Caddyfile` (the enforcing policy and its report path), `api/src/main.cpp` (`/csp-report` and the counter `/metrics` exposes), `tools/check-deploy.sh` (refuses a report-only policy or a report path no service serves) |
| F-065 | Image and dependency CVE scanning | C | ● | `tools/security-scan.sh` — Trivy 0.66.0 for secrets, fixable CVEs and deployment hardening; `tools/falsify/cases/security.ts` catches 3 planted defects and keeps a clean file green |
| F-066 | Judge host provisioned operator-manual (KVM not required with `isolate`) | C | ○ | not started: decided in ADR 0001 §1; it is operator work, so it lands as a runbook section rather than as code |
| F-067 | Discord failure notification from CI (secret set by the operator) | C | ◐ | `.github/workflows/ci.yml` — the job exists and is exercised through `simulate_failure`. **Blocked on the operator**: `DISCORD_WEBHOOK_URL` is not in the repository secrets, and no code change can set it |
| F-068 | Deploy job, gated on a tag rather than on every push | C | ○ | not started: needs the service images published to a registry; until then a tag job has nothing to deploy |
| F-069 | Single-server exposure contract: only the proxy is public, db/dashboards loopback, judge has no egress | A | ● | `tools/check-deploy.sh` + `tools/falsify/cases/deploy.ts` — 7/7 broken stacks rejected |
| F-070 | WAF + IP reputation at the edge, CRS evaluated **in-band** so the attack itself gets 403 | C | ● | `deploy/crowdsec/` — verified `Loaded 1 inband rules`, `Appsec listening on 0.0.0.0:7422` |
| F-071 | Encrypted offsite backups whose private key never touches the server | A | ● | `deploy/backup/run.sh` — `age` recipient only; refuses to run without a public key |
| F-072 | Registration and approval as runtime switches (no email dependency) | C | ● | `deploy/catalogue/settings.json` (`site.registration_open`, `site.registration_requires_approval`), read by `api/src/accounts.cpp`; live case "a closed registration refuses everyone" in `api/tests/accounts_test.cpp` |
| F-073 | Every C++ unit built by CMake presets under ASan + UBSan with `-Werror`, run through ctest, and analysed by clang-tidy — with the gate proven to fail | C | ● | `CMakePresets.json`, `cmake/Sanitizers.cmake`, `CMakeLists.txt` + `tools/cpp/gate.ts` (42 targets, 88 cases, 81 units analysed, 0 findings on the last run) + `tools/cpp/falsify.ts` **7/7**: use-after-free, signed overflow, stack overflow, `-Wunused-variable`, `bugprone-use-after-move`, a clean control and an empty tree — mandatory because ADR 0006 puts C++ on the internet-facing path |
| F-074 | Admin panel on its own listener, behind the proxy, with its own CSRF token and the session's account as the actor | C | ● | `api/src/admin_routes.cpp`, `api/src/csrf.cpp`, `api/tests/admin_panel_e2e.sh`; routed at `deploy/Caddyfile` and asserted by `tools/check-deploy.sh` + `tools/falsify/cases/deploy.ts`. The operator chose one container with two listeners over a separate container |
| F-075 | One plan render driver: `deploy/plan` is output of `deploy/schema/main.sql`, and the gate renders it again rather than trusting the commit | C | ● | `tools/render-plan.sh`, `tools/check-schema.sh`, `tools/falsify/cases/schema.ts` |
| F-076 | One falsification harness for the gates: each case is data, staged in a tree, and a mutation that does not land reads as setup failure | C | ● | `tools/falsify/runner.ts`, `tools/falsify/cases.ts`, `tools/falsify/self-check.ts` (`bun run falsify:runner-selfcheck`) |

---

## How to update this file

1. Find the row (or add one) — one row per user-visible capability, not per file.
2. Move the state only with evidence: a commit that landed the code **and** the gate that proves it.
3. `◐` means exactly what it says: code exists, proof does not. Do not round it up to ●.
4. Counts at the top are re-counted, not adjusted by hand.
