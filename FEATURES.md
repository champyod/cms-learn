# FEATURES.md — cms-learn

Every feature the project promises, with its real state and the artifact that proves it.

**Legend:** ● shipped · ◐ partial (code exists, gate or coverage missing) · ○ planned (decided, no
code)

**Rule for this file:** a row moves only when the evidence moves. Adding a feature here is a promise
an agent can be held to; marking one ● without evidence is the failure this file exists to prevent.

IDs are allocated by area and never reused, so gaps are reserved, not lost: `F-001–019` content,
`F-020–029` judge, `F-030–049` web, `F-050–059` accounts and security, `F-060–079` operations.

Counts at last update: **20 shipped · 4 partial · 27 planned** (51 features).

---

## Content and curriculum

| ID | Feature | State | Evidence |
|---|---|---|---|
| F-001 | Controlled topic vocabulary, TH+EN labels (33 topics) | ● | `content/topics.json` |
| F-002 | Routes are stages of tags with a difficulty band; there is no module layer | ● | `content/tracks.json` |
| F-003 | Two routes as parallel views of one bank — USACO ladder and IOI path — 4 stages each, every tag placed in both, difficulty shown as a number | ● | `content/tracks.json` |
| F-004 | Interface in Thai and English; a record made from the panel carries an optional second language, at least one language required, none privileged | ● | `content/schema/tracks.schema.json`, `content/tracks.json` |
| F-005 | Stage lists validated: unknown tag fails, tag twice in one stage fails, unplaced tag fails | ● | `tools/validate-content.ts` (cases 4, 5, 9) |
| F-006 | Task schema: topics, difficulty, limits, checker, origin, verification | ● | `content/schema/task.schema.json` |
| F-007 | Reachability rule — a task no stage can serve fails the gate | ● | `tools/validate-content.ts` (case 11) |
| F-008 | Publish gate — `published` refused without 3 verification methods and test data | ● | `tools/validate-content.ts` (case 2) |
| F-009 | Seed tasks with bilingual statements, machine-checked examples and compiling references | ● | `content/tasks/*/examples/`, `tools/check-references.sh` |
| F-010 | Test data with manifests per task | ○ | — (needed by F-012/F-013) |
| F-011 | AI generation pipeline with content-addressed cache key | ○ | spec: `docs/content-model.md` |
| F-012 | Brute-force differential verifier | ○ | spec: `docs/content-model.md` |
| F-013 | Limit measurement (time/memory with margin) | ○ | spec: `docs/content-model.md` |
| F-014 | Per-account unseen-task serving (topic × difficulty × band) | ○ | spec: `docs/content-model.md` |
| F-015 | Copy/plagiarism detection between classmates | ○ | — |

## Judge

| ID | Feature | State | Evidence |
|---|---|---|---|
| F-020 | `isolate` sandbox in a container with delegated cgroup v2 | ○ | ADR 0001 §1 |
| F-021 | C++ judge core with `testlib` checkers | ○ | ADR 0001 §2 |
| F-022 | Language runtimes: C++17, Rust 2021, Python 3 | ◐ | task schema declares them; no runner |
| F-023 | Interactive problem protocol | ○ | task schema has `checker.kind: interactive` |
| F-024 | Submission queue with per-submission status | ○ | — |
| F-025 | Abuse controls: egress disabled, fork bomb, output cap, wall-clock kill | ○ | — |

## Web

| ID | Feature | State | Evidence |
|---|---|---|---|
| F-030 | Design tokens, `judge mono` (contrast 15.41:1 body) | ● | `web/tokens.css` |
| F-031 | Thai-coverage monospace font, shipped with licence, verified by advance-width measurement | ● | `web/public/fonts/` |
| F-034 | Drogon + Inja + htmx app shell reading the tokens | ○ | ADR 0006 |
| F-035 | Path home: stage rail, current module, next task | ○ | layout decided in ADR 0002 |
| F-036 | Task view: statement render, samples, limits, topics | ○ | designed; statements are markdown |
| F-037 | Code entry: in-page editor + file upload (`.cpp/.rs/.py`) | ○ | ADR 0001 / decision 7 |
| F-038 | Submission result: verdict, per-test cost, diff of first failure | ○ | — |
| F-039 | Progress and weak-topic view | ○ | layout decided in ADR 0002 |
| F-040 | Dictionary wiring for TH/EN across every surface | ○ | preview proves the pattern |

## Accounts and security

| ID | Feature | State | Evidence |
|---|---|---|---|
| F-050 | Self-signup gated by an operator switch (registration open/close) | ○ | ADR 0003 §4 — email verification deliberately deferred; env contract in `deploy/.env.example` |
| F-051 | School-domain allowlist as config, fail-closed when empty | ○ | ADR 0002 §2 |
| F-052 | Teacher-approval gate with an on/off switch | ○ | ADR 0002 §2 |
| F-053 | Account state machine recorded in an audit trail | ○ | ADR 0002 §2 |
| F-054 | Rate limiting and lockout | ◐ | edge limits shipped in `deploy/Caddyfile` (auth 20/min, submissions 60/min per IP); per-account lockout is app-side and not built |
| F-055 | PII minimisation and retention policy (minors) | ○ | ADR 0002 §2 |

## Operations

| ID | Feature | State | Evidence |
|---|---|---|---|
| F-060 | CI running every gate and every falsification on each push | ● | `.github/workflows/ci.yml` |
| F-061 | Containerised stack (compose), no host installs | ● | `deploy/docker-compose.yml` — 10 services, contract asserted by `tools/check-deploy.sh` |
| F-062 | Ingress and TLS | ● | `deploy/Caddyfile` + `deploy/proxy/Dockerfile` — Caddy 2.11.7, auto-ACME, HSTS, per-IP rate limits; `caddy validate` run inside the built image |
| F-063 | Backups with a restore drill | ● | `deploy/backup/run.sh`, `restore-drill.sh` — verified end to end against Postgres 18.6 (250 users / 1000 submissions restored, live DB untouched) |
| F-064 | Observability: judge queue depth, submission latency, error rate | ◐ | `deploy/prometheus/prometheus.yml` — scrape targets and Grafana pinned; the api/judge `/metrics` endpoints do not exist yet |
| F-065 | Image and dependency CVE scanning | ● | `tools/security-scan.sh` — Trivy 0.66.0 for secrets, fixable CVEs and deployment hardening; `falsify-security-gate.sh` catches 3 planted defects and keeps a clean file green |
| F-066 | Judge host provisioned operator-manual (KVM not required with `isolate`) | ○ | ADR 0001 §1 |
| F-067 | Discord failure notification from CI (secret set by the operator) | ◐ | `.github/workflows/ci.yml` — wired and exercised via `simulate_failure`, `DISCORD_WEBHOOK_URL` not yet set |
| F-068 | Deploy job, gated on a tag rather than on every push | ○ | needs the three service images; CI reports "no target yet" |
| F-069 | Single-server exposure contract: only the proxy is public, db/dashboards loopback, judge has no egress | ● | `tools/check-deploy.sh` + `tools/falsify-deploy-gate.sh` — 7/7 broken stacks rejected |
| F-070 | WAF + IP reputation at the edge, CRS evaluated **in-band** so the attack itself gets 403 | ● | `deploy/crowdsec/` — verified `Loaded 1 inband rules`, `Appsec listening on 0.0.0.0:7422` |
| F-071 | Encrypted offsite backups whose private key never touches the server | ● | `deploy/backup/run.sh` — `age` recipient only; refuses to run without a public key |
| F-072 | Registration and approval as runtime switches (no email dependency) | ○ | ADR 0003 §4 — env contract exists in `deploy/.env.example`, api not built |
| F-073 | Every C++ unit built by CMake presets under ASan + UBSan with `-Werror`, run through ctest, and analysed by clang-tidy — with the gate proven to fail | ● | `cpp/` (presets, flags module, per-example ctest cases) + `tools/cpp/gate.ts` (2 targets, 3 cases, 2 units analysed) + `tools/cpp/falsify.ts` **7/7**: use-after-free, signed overflow, stack overflow, `-Wunused-variable`, `bugprone-use-after-move`, a clean control and an empty tree — mandatory because ADR 0006 puts C++ on the internet-facing path |
| F-074 | Admin surface on its own listener and container, with its own session scope | ○ | ADR 0006 §3 — the compose contract still describes 10 services; this is the one that will change the count |

---

## How to update this file

1. Find the row (or add one) — one row per user-visible capability, not per file.
2. Move the state only with evidence: a commit that landed the code **and** the gate that proves it.
3. `◐` means exactly what it says: code exists, proof does not. Do not round it up to ●.
4. Counts at the top are re-counted, not adjusted by hand.
