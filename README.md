# cms-learn

A competitive-programming learning platform: one shared task bank, two student-chosen paths
(**USACO ladder** and **IOI path**), and its own sandboxed judge. It runs beside the contest stack
(`cms-docker`) and never reads or writes contest data.

**Audience:** 30–60 students at launch, desktop and mobile, Thai and English.

## Start here

| You want | Read |
|---|---|
| What is being built, in what order, with what proof | [`docs/plan.md`](docs/plan.md) |
| What was decided and what it costs | [`docs/decisions/`](docs/decisions/) |
| Packages, images and tools, with versions and licences | [`docs/dependencies.md`](docs/dependencies.md) |
| The database and the invariants it enforces | [`docs/schema.md`](docs/schema.md) |
| The domain model | [`docs/content-model.md`](docs/content-model.md) |
| Server sizing, exposure and the operator checklist | [`docs/infra/single-server.md`](docs/infra/single-server.md) |
| Feature-by-feature state | [`FEATURES.md`](FEATURES.md) |
| Rules a contributor or agent must follow | [`AGENTS.md`](AGENTS.md) |

## What exists today

| Area | State | Where |
|---|---|---|
| Content model | **shipped** — 33 topics, 11 areas, 28 modules, 2 tracks, 8 stages, TH+EN labels | `content/` |
| Content gate | **shipped** — schema and reference integrity, 11/11 falsification cases | `tools/validate-content.ts` |
| Seed tasks | **2 drafts**, bilingual, references reproduce their own examples | `content/tasks/` |
| Design tokens and previews | **shipped** — `judge mono`, 12-direction gallery, live TH/EN switch | `web/tokens.css`, `design/` |
| Database schema | **written and exercised** — authority, audit, settings; applies clean on PostgreSQL 18.6 and every invariant is proven by a query | `deploy/schema/` |
| C++ workspace | **configured** — CMake project, vcpkg manifest with two overlay ports, capability and settings-snapshot headers, core tests | `CMakeLists.txt`, `core/` |
| `cmsctl` | **skeleton** — crate builds, no commands yet | `cmsctl/` |
| Deployment stack | **specified and gated** — 10 services, Caddy TLS, CrowdSec in-band CRS, encrypted backup with a drill | `deploy/`, `docs/infra/` |
| Web, api, judge binaries | **not built** — decisions recorded, no code | `docs/decisions/0004` |
| Schema *tooling* | **pending** — `pgschema` is not installed, so `plan` and `apply --plan` are unproven | `deploy/schema/` |

The data layer, the design foundation and the deployment contract are real and gated. The services
are decisions, not code. A document claiming otherwise is wrong.

## Architecture

```
  student · teacher (TH/EN)
        │  HTTPS
  ┌─────▼──────────────┐  proxy   Caddy 2.11.7 — TLS, rate limits, CrowdSec bouncer
  └─────┬──────────────┘
        │  internal network, no gateway
  ┌─────▼──────────────┐  web + api   C++23 · Drogon 1.9.14 · Inja · htmx
  └──┬──────────────┬──┘
     │              │
     │   ┌──────────▼─────────────┐  judge   C++ supervisor · isolate v2.7 · testlib
     │   │  no egress, cgroup v2  │          reached by the database and nothing else
     │   └──────────▲─────────────┘
  ┌──▼──────────────▼──┐  database   PostgreSQL 18.6 — settings, content, progress, verdicts
  └────────────────────┘
        ▲
        │  /run/cmslearn/admin.sock  (SO_PEERCRED, never public)
  cmsctl — Rust, no DATABASE_URL
```

Three languages, each for a reason it earns: **C++** for the api, the web layer, the shared domain
core and the judge, supervisor included; **Rust** for `cmsctl`, which is structurally unable to call a
C++ internal; **TypeScript** for the content gate and the CodeMirror island. The browser is HTML and CSS, because that is what browsers
execute — the full comparison is in `docs/decisions/0004`.

## Repository layout

```
core/       the shared C++ library: capabilities, settings, schema access, errors
references/ testlib checkers and reference solutions, built and run by check:cpp
cmake/      compiler options and sanitizer configuration, declared once
cmsctl/     Rust crate — the administrative client
web/        templates, tokens.css, fonts, static assets
content/    the bank: topics, modules, tracks, tasks — data, not code
deploy/     schema/, compose, Caddyfile, CrowdSec, backup, runbook
tools/      the gates and their falsification harnesses
design/     static previews: chosen direction, twelve-row gallery
docs/       plan, decisions, dependencies, schema, content model, infra
```

`content/` is the single source of truth. No module title, topic tag, track order or time limit is
hardcoded in a service.

## Getting started

Requirements: **Bun 1.3.14** (the gates are TypeScript), `python3` for the falsification harness,
`g++` to build seed references, plus `cmake`, `ninja` and `vcpkg` for the C++ workspace.

```bash
git clone https://github.com/champyod/cms-learn.git
cd cms-learn
bun install
bun run gate:all        # everything the CI runs, including the deployment contract
```

What runs today:

```bash
bun run validate            # content integrity, with the counts it scanned
bun run check:references    # every reference builds under -Werror and reproduces its examples
bun run check:deploy        # exposure, isolation, pinning, Caddyfile, Prometheus
bun run check:security      # secrets, fixable CVEs, deployment hardening
bun run check:cpp           # every C++ unit under ASan + UBSan, warnings fatal, runnable ones executed
bun run check:scaffold      # the C++ workspace and the pinned dependency set
```

Static previews open in a browser with no server:

- `design/judge-mono.html` — the chosen direction, TH/EN switch, live Thai measurements
- `design/theme-gallery.html` — twelve directions over the same markup

## Gates

| Gate | Proves |
|---|---|
| `bun run validate` | every reference resolves; no prerequisite cycle; no published task without machine evidence; every task is reachable by a track stage |
| `bun run validate:falsify` | the gate exits non-zero on 11 real defects, including an empty bank |
| `bun run check:references` | every reference compiles under `-Werror` and reproduces its own examples |
| `bun run check:deploy` | only the proxy is public, the judge has no egress, every image is pinned, and the Caddyfile and Prometheus config are valid for the exact versions in compose |
| `bun run check:security` | no secret in the tree, no dependency with a fixed CVE, no deployment misconfiguration at MEDIUM+ |
| `bun run check:cpp` | every C++ unit compiles under ASan + UBSan with `-Werror` and `_GLIBCXX_ASSERTIONS`, and every runnable one is executed |
| `bun run check:cpp:falsify` | a use-after-free, a signed overflow, an out-of-bounds read and a warning each fail with the right reason; a clean file stays green |
| `bun run check:scaffold` | the C++ workspace configures, the overlay ports are present, the dependency manifest resolves, and the CLI crate is well formed |

A green run prints what it scanned, so an empty bank cannot pass as coverage. Every falsification
harness works on a throwaway copy and never mutates the working tree.

## Authoring content

**Add a module** — append to `content/modules.json` with an `area`, at least one `topics` id, and
`prereqs` that already exist. Then place it in a track stage; a module no stage places fails the gate.

**Add a task** — create `content/tasks/<slug>/` with `task.json`, one statement per locale,
`solutions/main.cpp` and `examples/N.in`/`N.out`. A task stays `draft` until verification records all
three methods (`reference-pass`, `bruteforce-differential`, `limits-measured`) and test data exists.

Model, cache key and serving policy: [`docs/content-model.md`](docs/content-model.md).

## Design constraints that are not negotiable

1. **Thai has no uppercase.** `text-transform: uppercase` is a no-op on Thai; hierarchy comes from
   size, weight and spacing, and only Latin labels get `--tracking-label`.
2. **Thai marks stack.** Body line-height 1.7, never below 1.45, or vowels and tone marks clip.
3. **Monospace Thai is rare.** Five open families exist. This build ships Arundina Sans Mono (GPL-2+)
   and deliberately does not subset it.
4. **One token source.** `web/tokens.css`; Tailwind maps to those variables instead of redefining
   them.

## Status

Open decisions: the judge host provider, whether teacher approval is on for the pilot cohort, and the
project licence.

## Licence

Not yet chosen — the operator decides, and nothing is relicensed implicitly. The vendored font
`web/public/fonts/ArundinaSansMono.ttf` is **GPL-2+**, with its licence text beside it.
