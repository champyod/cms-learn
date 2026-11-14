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
| openssl | **3.6.5#2**, transitive — never named in any manifest | Apache-2.0 | TLS under `curl`, `libpq`, `trantor` (Drogon), `aws-c-cal`, `s2n` and `aws-sdk-cpp`. Its version is decided by the dependency graph, not by us |

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
resolution to vcpkg's manifest mode, which bootstraps vcpkg if needed, installs what the manifest
declares, and links from `build/vcpkg_installed`.

The manifest is **per service**, not one list for everything: `api/vcpkg.json` and its
`api/vcpkg-configuration.json` sit beside the service that consumes them, because vcpkg looks for both
files in the manifest directory and does not search upward. The presets point at it with
`VCPKG_MANIFEST_DIR`. A second service gets its own pair, and only then, so nothing is declared for a
directory that does not exist.

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

All three are disposable; the next configure rebuilds whatever it needs. The baseline in
`api/vcpkg-configuration.json`, not the cache, is what decides versions.

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
| gcc / g++ | **16.2.0**, from the `gcc:16.2.0-trixie` image | GPL-3.0-or-later WITH GCC-exception-3.1 | every C++ translation unit: the workspace build, the gate and the api image |
| pgschema | 1.13.4 | Apache-2.0 | schema plan and apply |
| ccache | 4.11.3 | GPL-3.0 | compiler cache for the falsification harness |
| isolate | v2.7 | GPL-2.0 | sandbox |
| testlib | 0.9.41 | MIT | checker library |
| Trivy | 0.66.0 | Apache-2.0 | security gate |
| age | current | BSD-3-Clause | backup encryption |
| Arundina Sans Mono | — | GPL-2+ | Thai and Latin monospace, licence text beside the file |

The compiler is pinned by image tag rather than by a distribution package: the api image builds on
`gcc:16.2.0-trixie`, CI runs that same tag, and `cmake/ProjectSettings.cmake` refuses to configure under a
compiler that is not GNU or is older than `CMS_MIN_GCC_MAJOR` (16). A development host therefore needs
g++ 16 or newer; the gate's own run reports `g++ (GCC) 16.2.1`.

### Compiler cache

`tools/cpp/falsify.ts` builds a baseline tree and one tree per planted defect, and every tree carries the same
source except the file a case plants. Without a compiler cache that is the same sanitizer build nine times, and
the gate that proves the other gates stops being run — which is how a gate stops being true.

The harness does **not** need `ccache`. It is an optimisation only: a machine without it builds the same tree once
and takes the same time it always did, and the harness refuses only when a launcher is configured and the binary it
names is missing (a broken environment, not a plain one).

**Measured, 2026-11-14: 0 hits of 476 compiles.** The cache key is the preprocessed source and the exact command
line, and the preprocessed text carries each tree's absolute path, so a tree at a fresh temporary path can never
hit — `CCACHE_BASEDIR` does not rewrite the line markers. The launcher therefore buys same-path reuse (a re-run in
the same directory, a rebuild after a comment change), and the speed-up the falsification harness actually relies
on is a single shared tree rebuilt in place, not this cache. Do not add it expecting that win.

To get it: install the distribution package (`pacman -S ccache`, `apt install ccache`, `brew install ccache`),
or take the release at tag `v4.11.3` from the project's releases and build it with CMake — the harness only
needs the binary on `PATH`.

### Obtaining the pinned schema tool

The gates look in `${XDG_DATA_HOME:-$HOME/.local/share}/cms-learn/tools` first, then the
package caches. A copy there survives a cache clean, which has already removed the pin twice.

`pgschema` renders and applies the migration plan in `deploy/plan`, and the plan is only comparable if the
version that renders it is the version the plan was rendered by. The gates therefore resolve this pin
through one place, `tools/pinned-pgschema.sh`, which refuses to run any other version rather than reporting
a mismatch as drift. The version above is the pin, and this table is what the refusal names.

The resolver also looks, best-effort, in the local package caches a machine may still be holding the
package in: the AUR helper caches (yay, paru) and `/var/cache/pacman/pkg` on Arch, `/var/cache/apt/archives`
on Debian and Ubuntu, `/var/cache/dnf` and `/var/cache/yum` on Fedora and RHEL, and Homebrew's cache on
macOS. An entry counts only when it is executable and reports exactly the pinned version — a `.pkg.tar.zst`,
`.deb`, `.rpm` or bottle is not one, so a cache that keeps only the archive helps nobody and is skipped.

To get it:

```bash
# 1. a 1.13.4 binary you can lay hands on, in a directory of its own
#    (Arch, if the package cache still holds the unpacked binary beside the package)
ls ~/.cache/yay/pgschema-bin/            # pgschema-bin-1.13.4-x86_64 is the binary itself

# 2. otherwise take the release built for this platform from the project's releases, at tag v1.13.4,
#    and unpack it; nothing here downloads it for you

# 3. check it and put it where the gates look first
<dir>/pgschema --version                 # must print 1.13.4
export PATH="<dir>:$PATH"                # the resolver checks the version, not the directory
# or name it outright, which the resolver also accepts:
export CMSLEARN_PGSCHEMA=/<dir>/pgschema
```

The refusal fires before `initdb`, before a render and before an apply: a version mismatch cannot leave a
half-built plan behind. If the pin moves, change this table and re-render:
`bash tools/render-plan.sh` (which resolves the same pin).

## Rules

- Production runs containers only. A host package is a decision to raise, not an implementation detail.
- Dev may install natively: the bootstrap discovers a package manager and falls back to the pinned
  release binary for the detected OS and architecture.
- A new dependency needs a row here and a line in the ADR or plan step that introduced it, in the same
  commit.
