# Dependencies

Every package, image and tool the project uses, with the version it is pinned to and the licence it
carries. A dependency that is not in this table is not allowed in the build.

## C++ — pinned by vcpkg baseline `ab9a05e7`

| Package | Version | Licence | Used for |
|---|---|---|---|
| drogon | **1.9.14** (overlay port) | MIT | HTTP server, routing, `orm::DbClient` Postgres pool, sessions |
| inja | baseline `ab9a05e7` | MIT | template rendering |
| simdjson | **5.0.8** (overlay port, `default-features: false`, `utf8-validation`) | Apache-2.0 | request and JSON parsing |
| libsodium | baseline `ab9a05e7` | ISC | sealing credentials with `CMSLEARN_SEAL_KEY` |
| spdlog | baseline `ab9a05e7` | MIT | structured logging |
| sqlpp23 | **0.70**, pinned by tag | BSD-2-Clause | compile-time-checked SQL; statements are executed through a Drogon connector written here |
| catch2 | baseline `ab9a05e7`, `tests` feature only | BSL-1.0 | core and judge tests |
| aws-sdk-cpp | baseline `ab9a05e7`, `s3` feature only | Apache-2.0 | `PutObject`, `GetObject`, `HeadObject`, `ListObjectsV2` for task assets |

Exact baseline versions are resolved at configure time by vcpkg and recorded in `vcpkg-lock` output;
the two overlay ports are the ones that differ from the registry.

## Rust — `cmsctl` only

| Crate | Version | Licence | Used for |
|---|---|---|---|
| clap | 4.5 | MIT / Apache-2.0 | argument parsing |
| serde | 1 | MIT / Apache-2.0 | wire types |
| serde_json | 1 | MIT / Apache-2.0 | control-socket payloads |

No database crate is permitted: `cmsctl` holds no `DATABASE_URL`, and the build gate fails if one is
added.

## TypeScript

| Package | Version | Licence | Used for |
|---|---|---|---|
| bun | 1.3.14 | MIT | runtime for the gates only |
| ajv | ^8.17.1 | MIT | JSON Schema validation in the content gate |
| @types/node | ^22.10.2 | MIT | types for the gate scripts |
| tailwindcss | 4.3.3 | MIT | build-time CSS generation |
| @tailwindcss/cli | 4.3.3 | MIT | the `build:css` command |
| daisyui | 5.7.50 | MIT | component classes, CSS only |
| htmx | 4.0.0, vendored | 0BSD | partial page updates; no build step |
| CodeMirror | 6 | MIT | the editor island |

## Images

| Image | Tag | Licence | Role |
|---|---|---|---|
| `postgres` | 18.6 | PostgreSQL | database |
| `caddy` | 2.11.7 | Apache-2.0 | TLS, routing, rate limits |
| `crowdsecurity/crowdsec` | v1.8.1 | MIT | in-band CRS AppSec and IP reputation |
| `prom/prometheus` | v3.15.1 | Apache-2.0 | metrics |
| `grafana/grafana` | 13.2.4 | AGPL-3.0 | dashboards, operator only |
| `prom/node-exporter` | v1.8.2 | Apache-2.0 | host metrics |
| `seaweedfs` | 4.51 | Apache-2.0 | S3-compatible object store for task text, source and test data |

Application images (`web`, `api`, `judge`, `schema`, `backup`) are built here and pinned by digest in
`release.toml`; none exists yet.

## Tools

| Tool | Version | Licence | Used for |
|---|---|---|---|
| pgschema | 1.13.4 | Apache-2.0 | schema plan and apply |
| isolate | v2.7 | GPL-2.0 | sandbox |
| testlib | 0.9.41 | MIT | checker library |
| Trivy | 0.66.0 | Apache-2.0 | security gate |
| age | current | BSD-3-Clause | backup encryption |
| Arundina Sans Mono | — | GPL-2+ | Thai and Latin monospace, licence text beside the file |

## Rules

- Production runs containers only. A host package is a decision to raise, not an implementation detail.
- Dev may install natively: the bootstrap discovers a package manager and falls back to the pinned
  release binary for the detected OS and architecture.
- A new dependency needs a row here and a line in the ADR or plan step that introduced it, in the same
  commit.
