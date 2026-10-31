# Dependencies

Every package, image and tool the project uses, with the version it is pinned to and the licence it
carries. A dependency that is not in this table is not allowed in the build.

## C++ — pinned by vcpkg baseline `ab9a05e7`

| Package | Version | Licence | Used for |
|---|---|---|---|
| drogon | **1.9.14** (overlay port) | MIT | HTTP server, routing, `orm::DbClient` Postgres pool, sessions |
| inja | baseline `ab9a05e7` | MIT | template rendering — declared, not yet included by any source file |
| simdjson | **5.1.0** (overlay port, `default-features: false`, `utf8-validation`) | Apache-2.0 | request and JSON parsing — declared, not yet included by any source file |
| libsodium | baseline `ab9a05e7` | ISC | sealing credentials with `CMSLEARN_SEAL_KEY` — declared; the sealing code is not written |
| spdlog | baseline `ab9a05e7` | MIT | structured logging — declared, not yet included by any source file |
| sqlpp23 | **0.70**, the version the pinned baseline resolves | BSD-2-Clause | compile-time-checked SQL; executed through the connector in `db/` |
| catch2 | baseline `ab9a05e7`, `tests` feature only | BSL-1.0 | core, db and judge tests |
| aws-sdk-cpp | baseline `ab9a05e7`, `default-features: false`, `s3` feature only | Apache-2.0 | `PutObject`, `GetObject`, `HeadObject`, `ListObjectsV2` for task assets — declared, not yet included by any source file |
| openssl | **3.6.5#2**, transitive — never named in `vcpkg.json` | Apache-2.0 | TLS under `curl`, `libpq`, `trantor` (Drogon), `aws-c-cal`, `s2n` and `aws-sdk-cpp`. Its version is decided by the dependency graph, not by us |

`aws-sdk-cpp` declares `dynamodb`, `kinesis` and `s3` as default features, so naming the port without
`default-features: false` compiles three service SDKs to use one. The manifest turns the defaults off
and enables `s3` alone; the dependency list and the version of each service are visible in
`~/.local/share/vcpkg/ports/aws-sdk-cpp/vcpkg.json`.

Exact baseline versions are resolved at configure time by vcpkg and recorded in `vcpkg-lock` output;
the two overlay ports are the ones that differ from the registry.

### OpenSSL 4.x is deliberately not taken

OpenSSL 4.1.0 exists upstream and this project stays on 3.6.5. That was measured, not assumed: a probe
built 4.1.0 from source, and while the library itself compiles, `s2n` 1.7.6 does not — it dereferences
`ASN1_IA5STRING`, which 4.x made opaque, and calls `ASN1_STRING_data`, which 4.x removed. `aws-c-io`,
`aws-crt-cpp` and `aws-sdk-cpp` all sit on `s2n`, so the S3 path is the first casualty. Adopting 4.1
today would mean owning patches to a TLS library in the security path. The trigger to revisit is s2n
shipping 4.x support; the five port-recipe changes 4.1 needs are already known and small.

### How the packages arrive

The presets name `cmake/vcpkg-toolchain.cmake` as their toolchain. It reads `VCPKG_ROOT` and hands
resolution to vcpkg's manifest mode, which bootstraps vcpkg if needed, installs what `vcpkg.json`
declares, and links from `build/vcpkg_installed` — one directory shared by both presets, so the two
configurations do not build Drogon and the S3 client twice.

There is no separate install command to run. Four cases, four outcomes:

| `VCPKG_ROOT` | a target needs packages | outcome |
|---|---|---|
| unset | no — `CMS_WITH_API` and `CMS_WITH_JUDGE` are off | configures; nothing is resolved |
| unset | yes | fails, naming `VCPKG_ROOT` |
| set, and the checkout contains `scripts/buildsystems/vcpkg.cmake` | either | resolves through vcpkg |
| set, but the path is wrong | either | fails, naming the path |

`CMakeUserPresets.json` holds a machine-specific `VCPKG_ROOT` and is not committed. The `tests` manifest
feature is requested by the preset, so Catch2 arrives with the packages instead of being skipped.

### Preparing a development host

`VCPKG_ROOT` must name a vcpkg checkout, because that checkout is the package registry: the `ports/`
tree is where every package definition lives. A binary alone is not enough — the distribution package
ships only `/usr/bin/vcpkg`, and invoking it without a checkout fails with
`Could not detect vcpkg-root`. So installing `vcpkg` from a distribution adds a command to `PATH` but
does not remove the checkout.

```bash
git clone https://github.com/microsoft/vcpkg.git ~/.local/share/vcpkg
~/.local/share/vcpkg/bootstrap-vcpkg.sh -disableMetrics
```

`~/.local/share/vcpkg` is also the path the Arch package's `/etc/profile.d/vcpkg.sh` exports, so either
route leaves `VCPKG_ROOT` pointing at the same place. Nothing here runs in production: the server has
containers only, and the build images carry their own toolchain.

### Caches, and how to clear them

Nothing is cached inside the repository except the installed tree:

| What | Where | Cleared by |
|---|---|---|
| built packages | `build/vcpkg_installed` | `rm -rf build` |
| source and tool downloads | `$VCPKG_ROOT/downloads` | `rm -rf "$VCPKG_ROOT/downloads"` |
| binary archives | `$XDG_CACHE_HOME/vcpkg/archives`, else `~/.cache/vcpkg/archives` | `rm -rf ~/.cache/vcpkg/archives` |

All three are disposable; the next configure rebuilds whatever it needs. The `vcpkg.json` baseline,
not the cache, is what decides versions.

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
| bun | 1.3.14, pinned by `packageManager` | MIT | runtime for the gates only |
| ajv | ^8.17.1 | MIT | JSON Schema validation in the content gate |
| @types/node | ^26.6.6 | MIT | types for the gate scripts |
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
| `crowdsecurity/crowdsec` | v1.8.2 | MIT | in-band CRS AppSec and IP reputation |
| `prom/prometheus` | v3.15.1 | Apache-2.0 | metrics |
| `grafana/grafana` | 13.3.0 | AGPL-3.0 | dashboards, operator only |
| `prom/node-exporter` | v1.9.1 | Apache-2.0 | host metrics |
| `seaweedfs` | 4.51 | Apache-2.0 | S3-compatible object store for task text, source and test data |

Every one of these is pinned by digest in `release.toml`, and the `FROM` lines in `deploy/` carry digests too. A tag without its matching digest changes nothing: the digest is what runs.

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
