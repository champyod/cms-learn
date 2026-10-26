# cms-learn

[![CI](https://github.com/champyod/cms-learn/actions/workflows/ci.yml/badge.svg)](https://github.com/champyod/cms-learn/actions/workflows/ci.yml)

A competitive-programming learning platform: one shared task bank, two student-chosen paths
(**USACO ladder** and **IOI path**), and its own sandboxed judge — built beside the contest stack
(`cms-docker`, `cms.mwit.ac.th`) rather than inside it.

Sibling naming is deliberate: `cms-learn` sorts next to `cms-*` in the hub and reads as the learning
arm of the same family. It never reads or writes contest data.

**Audience:** 30–60 students at launch, desktop and mobile, Thai and English.

---

## What exists today

| Area | State | Evidence |
|---|---|---|
| Content model | **shipped** — 33 topics, 11 areas, 28 modules, 2 tracks, 8 stages, TH+EN labels | `content/` |
| Content gate | **shipped** — schema + reference integrity, **11/11** falsification cases | `tools/validate-content.ts` |
| Seed tasks | **2 drafts** with bilingual statements and reference solutions that reproduce their examples | `content/tasks/` |
| Design tokens | **shipped** — `judge mono`; contrast 15.41:1 body; Thai-mono font verified by measurement | `web/tokens.css` |
| Design previews | **shipped** — 12-direction gallery, plus the chosen direction with live TH/EN switching | `design/` |
| API, judge, web app | **not built** — decisions recorded, no code yet | `docs/decisions/` |
| Deployment stack | **specified and gated** — 10-service compose, Caddy TLS, CrowdSec in-band CRS, Postgres 18.6, Prometheus/Grafana, encrypted backups with a drill; **verified** by `check:deploy` + 7/7 falsification | `deploy/`, `docs/infra/single-server.md` |
| Auth, pipeline | **not built** — specified in the epic and ADRs | `../epics/003-cms-learn.md` |

Honest summary: the **data layer, design foundation and deployment contract** are real and gated; the
**services are decisions, not code**. Any document implying otherwise is wrong.
[`FEATURES.md`](FEATURES.md) is the per-feature registry.

---

## Architecture

Decided in [`docs/decisions/0001-scope-stack-sandbox.md`](docs/decisions/0001-scope-stack-sandbox.md)
and [`0002-theme-and-auth.md`](docs/decisions/0002-theme-and-auth.md):

```
  student (TH/EN)
        │
        ▼
  ┌───────────────────────┐   web/    Next.js, tokens from web/tokens.css   (planned)
  └───────────┬───────────┘
              │ session cookie over HTTP
  ┌───────────▼───────────┐   api/    Rust + axum 0.8: auth, progress,
  │                       │           task serving                          (planned)
  └───┬───────────────┬───┘
      │               │
      │        ┌──────▼──────────────────────┐
      │        │ content pipeline            │   generate → verify → cache
      │        │ content-addressed artifacts │   → publish                  (planned)
      │        └──────┬──────────────────────┘
      │               │
  ┌───▼───────────┐   │
  │ judge/        │   │
  │ isolate +     │   │
  │ testlib (C++) │   │
  └───────┬───────┘   │
          ▼           ▼
  ┌─────────────────────────────────────────┐
  │ content/  topics · modules · tracks ·    │   the single source of truth
  │           tasks                          │   (shipped)
  └─────────────────────────────────────────┘
```

**Why this split.** `testlib` (the CP-standard checker library) and `isolate` semantics are C++, so
the judge core is C++ and reuses packaged, domain-standard primitives instead of re-implementing
them; the mitigation is ASan/UBSan, libFuzzer and clang-tidy on every judge build. Services are Rust
for memory safety on the internet-facing path. The UI is TypeScript because the admin panel already
ships Next 16.1.1 + Tailwind 4 + Radix — a second component vocabulary beside it would be a
duplicated design system.

**Judging model.** `isolate` inside a container with a delegated cgroup v2 path: the same mechanism
the contest stack operates today. A Firecracker microVM was chosen first and withdrawn — it needs
KVM plus host-level kernel, microcode and jailer setup, which conflicts with the hub's "containers
only, no host scripts" rule.

---

## Repository layout

```
api/        Rust service — HTTP API, auth, progress, task serving            (planned)
judge/      C++ core — sandbox launcher (isolate), checker (testlib)        (planned)
web/        TypeScript UI — Next.js; tokens.css is the design source        (tokens only)
content/    topics · modules · tracks · tasks — data, not code              (shipped)
tools/      repo gates                                                      (shipped)
design/     static previews: chosen direction, twelve-row gallery           (shipped)
docs/       ADRs and the content model                                      (shipped)
```

`content/` is the single source of truth. No module title, topic tag, track order or time limit is
ever hardcoded in a service.

---

## Getting started

Requirements: **Bun ≥ 1.4** (the gate is a Bun script), `python3` (falsification harness), `g++` if
you want to build the seed reference solutions.

```bash
git clone https://github.com/champyod/cms-learn.git
cd cms-learn
bun install
```

There is no dev server yet — the app does not exist. What runs today is the content gate:

```bash
bun run validate           # schemas, references, cycles, reachability, verification status
bun run validate:falsify   # proves the gate can fail (11 deliberate defects)
```

Expected output:

```
content gate — OK · scanned: topics 33 · areas 11 · modules 28 · tracks 2 · stages 8 · tasks 2 (draft 2, verified 0, published 0, rejected 0)
```

Static previews open directly in a browser, no server needed:

- `design/judge-mono.html` — the chosen direction, TH/EN switch, live Thai rendering measurements
- `design/theme-gallery.html` — all twelve directions over the same markup

---

## Gates

| Gate | Command | Proves |
|---|---|---|
| Content integrity | `bun run validate` | every reference resolves; no prerequisite cycle; no published task without machine evidence; every task reachable by a track stage |
| Content gate falsifiability | `bun run validate:falsify` | the gate exits non-zero on 11 real defects, including an empty bank |
| Reference solutions | `bun run check:references` | every reference builds under `-Werror` and reproduces its task's own examples |
| Reference gate falsifiability | `bun run check:references:falsify` | the reference gate exits non-zero on a wrong expected output, a missing example set, a non-compiling reference, and an empty bank |
| Deployment contract | `bun run check:deploy` | only the proxy is public, the database and dashboards are loopback-only, the `internal` network has no gateway, the judge cannot reach the internet, every image is pinned, and the Caddyfile + Prometheus config are valid **for the exact versions in compose** |
| Deploy gate falsifiability | `bun run check:deploy:falsify` | the deploy gate rejects **7** broken stacks, each with its own reason (public database port, judge with egress, unpinned image, dropped `no-new-privileges`, leaked network gateway, unordered Caddy directive, wrong proxy port) |

All of them, in order: `bun run gate:all`. The first four (`bun run gate`) need no docker; the
deployment contract does.

A green run prints the counts it scanned, so an empty or partial bank cannot pass as coverage. Both
falsification harnesses work on a throwaway copy and never mutate the working tree.

## Continuous integration

`.github/workflows/ci.yml` runs on every push to `master`, every pull request, and on demand:

| Job | What it does |
|---|---|
| `content` | `bun install --frozen-lockfile`, then the content gate and its falsification |
| `references` | builds every reference solution and runs it against its own examples, then falsifies the gate |
| `deployment contract` | asserts the compose contract (exposure, isolation, pinning) and validates the real Caddyfile and Prometheus config, then falsifies the gate with 7 broken stacks |
| `deployment` | reports that **no deploy target exists yet** (the three service images are not built) rather than implying a pipeline |
| `discord` | on failure of any gate job, posts one message with the branch, short SHA, actor and run URL |

**Discord notifications** need `DISCORD_WEBHOOK_URL` set as a repository secret. The workflow never
echoes it and never fails when it is absent — it prints a notice instead, because a green step that
told nobody is worse than an honest skip. Set it yourself:

```bash
gh secret set DISCORD_WEBHOOK_URL     # paste the webhook when prompted; it never enters chat
```

To test the notification path without breaking the build, dispatch a run that forces the red path:

```bash
gh workflow run ci.yml -f simulate_failure=true
```

**Deployment** is specified and gated, not running: `deploy/` holds the single-server stack, and
`docs/infra/single-server.md` + ADR 0003 record why each piece is what it is. `web`, `api` and
`judge` have no images yet, so `docker compose up` stops at them — CI says that out loud instead of
implying a pipeline. When the images exist, the deploy job lands in the `deployment` position gated
on a tagged release, never on every push.



---

## Authoring content

**Add a module** — append to `content/modules.json` with an `area`, at least one `topics` id from
`content/topics.json`, and `prereqs` that already exist. Then place it in at least one track stage in
`content/tracks.json`; a module no track places fails the gate.

**Add a task** — create `content/tasks/<slug>/` containing:

```
task.json            # id == directory name; topics; difficulty 800–3500 (step 100);
                     # limits; checker; origin; verification
statement.en.md
statement.th.md      # one file per locale claimed in task.json
solutions/main.cpp   # the reference the verifier runs
```

Then run the gate. A task may sit at `status: draft` indefinitely; it becomes servable only at
`published`, which the gate refuses unless verification records all three methods
(`reference-pass`, `bruteforce-differential`, `limits-measured`) and test data exists.

Model, cache key and serving policy: [`docs/content-model.md`](docs/content-model.md).

---

## Design constraints that are not negotiable

Each of these cost a release the first time it was missed, so they live in the token layer:

1. **Thai has no uppercase.** `text-transform: uppercase` is a no-op on Thai, so hierarchy comes from
   size, weight and spacing. Only Latin labels get `--tracking-label`.
2. **Thai marks stack.** Body line-height 1.7; never below 1.45, or vowels and tone marks clip.
3. **Monospace Thai is rare.** Five open families exist. This build ships Arundina Sans Mono
   (GPL-2+) and deliberately does not subset it: subsetting hands Latin to the system mono
   (5.91 px/char) while Thai advances 8.44 px/char, and mixed columns drift.
4. **One token source.** `web/tokens.css`; Tailwind maps to those variables instead of redefining
   them.

---

## Decisions and status

- ADRs: [`0001`](docs/decisions/0001-scope-stack-sandbox.md) scope, stack, sandbox ·
  [`0002`](docs/decisions/0002-theme-and-auth.md) theme and account model
- Epic and step ledger: `../epics/003-cms-learn.md` (hub root)
- Feature registry: [`FEATURES.md`](FEATURES.md)
- Agent rules for this repo: `AGENTS.md`

Open decisions: judge host provider · whether teacher approval is on for the pilot cohort · project
licence.

---

## Licence

Source licence not yet chosen — the operator decides, and nothing is relicensed implicitly. The
vendored font `web/public/fonts/ArundinaSansMono.ttf` is **GPL-2+**, with its licence text beside it.
