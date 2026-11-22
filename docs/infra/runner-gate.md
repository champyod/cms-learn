# Running the whole C++ gate as the runner user

`check:cpp` is the C++ half's verdict, and it belongs to one image: `cms-runner:gcc16`, built from
`deploy/runner/Dockerfile` (ADR 0043). The image declares `USER runner` (uid/gid 1000) and names every
path its gate may write. This page is the recipe for running the **whole** gate inside that image as
that user — configure, build, the cases, the static analysis — with the services the cases need, and
what the recipe produced on the runner.

A build inside the image proves the toolchain. It does not prove the gate: the gate is configure +
ctest + clang-tidy, and eleven `api_*_e2e` cases start a scratch PostgreSQL cluster, apply the schema
plan with the pinned `pgschema`, and one of them starts a container through the Docker socket. When
those are missing the cases fail or skip, the gate stops before the analysis, and the half that costs
the wall time is the half nobody saw.

## What the image carries, and what this recipe adds

| The gate needs | Where it comes from |
|---|---|
| g++ 16.2.0, clang 23.1.6, clang-tidy 23.1.6, cmake, ninja, bun 1.3.14 | the image |
| `pgschema` 1.13.4 — every `api_*_e2e` case applies `deploy/plan` through `tools/apply-plan.sh`, which refuses any other version | this recipe: the pinned release binary, digest checked, on `PATH` |
| `initdb`, `pg_ctl`, `psql` for those same cases | this recipe: `apt-get install postgresql postgresql-client` in a root phase |
| a Docker CLI and the daemon socket for the store proof (`api_object_readers_e2e`) | this recipe: the CLI from `docker:cli` in a volume, the socket bind-mounted, its group added |
| the published static asset `web/public/app.css` — `api_render_e2e` asserts `/static/app.css` is served with the published bytes, and that file is generated, not committed | this recipe: `bun install --frozen-lockfile && bun run build:css` before the gate, with `bunx` on `PATH` |
| a vcpkg checkout (`VCPKG_ROOT`) and the installed package tree | cache volumes **this image** built, at `/home/runner/.vcpkg` and `<tree>/build/vcpkg_installed` |

The rule the two phases keep: the root phase adds only tools, and the ones it adds are the tools the
gates resolve outside the image (`deploy/runner/README.md` names them and why they are not in it).
Everything the verdict is a claim about — compiler, analyser, runtime — is the image's, unchanged.

## The recipe

Prepare the two tools a volume can carry once, executable by the runner user:

```sh
# the Docker CLI the store proof talks to the daemon with
docker volume create cms-runner-tools
docker run --rm --entrypoint cp -v cms-runner-tools:/out docker:cli /usr/local/bin/docker /out/docker

# the pinned schema tool, digest-checked against deploy/schema/Dockerfile
docker run --rm --network host -v cms-runner-tools:/out alpine sh -c '
  apk add --no-cache curl >/dev/null
  curl -fsSL -o /out/pgschema \
    https://github.com/pgplex/pgschema/releases/download/v1.13.4/pgschema-1.13.4-linux-amd64
  echo "abce75a704ce411a8f9d1dcc786573d7e0e6dcc6b060a0e6b66a1550ed734bdb  /out/pgschema" | sha256sum -c -
  chmod 0555 /out/pgschema'

docker run --rm -v cms-runner-tools:/out alpine chown 1000:1000 /out/docker /out/pgschema
```

Then one container: a root phase that installs the rest, and the gate as `runner`:

```sh
docker run --rm --name cms-gate --network host --group-add 0 --user 0 \
  -v <tree>:/src \
  -v <vcpkg-installed>:/src/build/vcpkg_installed \
  -v <vcpkg-checkout>:/home/runner/.vcpkg \
  -v <cache>:/home/runner/.cache \
  -v cms-runner-tools:/home/runner/bin \
  -v /var/run/docker.sock:/var/run/docker.sock \
  -w /src cms-runner:gcc16 bash -s <<'IN'
apt-get update -qq && apt-get install -y -qq postgresql postgresql-client curl
ln -sf /opt/bun-linux-x64/bun /usr/local/bin/bunx
mkdir -p /home/runner/.cache/{tmp,ccache,bun}
chown -R 1000:1000 /src /home/runner/.cache /home/runner/.vcpkg
exec setpriv --reuid=1000 --regid=1000 --groups=0 --inh-caps=-all --bounding-set=-all \
  env HOME=/home/runner XDG_CACHE_HOME=/home/runner/.cache \
      CCACHE_DIR=/home/runner/.cache/ccache BUN_INSTALL_CACHE_DIR=/home/runner/.cache/bun \
      TMPDIR=/home/runner/.cache/tmp VCPKG_ROOT=/home/runner/.vcpkg \
      PATH=/home/runner/bin:/usr/local/bin:/usr/bin:/bin \
  bash -c 'cd /src && if [ ! -s web/public/app.css ]; then
             bun install --frozen-lockfile && bun run build:css
           fi && bun run check:cpp'
IN
```

`<tree>`, `<cache>`, `<vcpkg-checkout>` and `<vcpkg-installed>` are volumes or directories owned by
uid/gid 1000. A fresh vcpkg cache builds the whole package tree inside the container on the first
run; later runs reuse it.

## The traps, and why the shape is this shape

- **`su runner` loses the socket's group.** `su` and `runuser` reset the supplementary groups to the
  user's own, so the container's `--group-add` is gone by the time the gate runs and every Docker call
  answers `permission denied while trying to connect to the docker API at unix:///var/run/docker.sock`
  — while the same container, run as root, reaches the daemon fine. Drop with
  `setpriv --groups=<socket gid>` instead: it changes the uid without resetting the added group, and
  the gate then reports `groups=1000 0`.
- **The socket's group is the engine's.** A Docker Desktop engine presents the socket as
  `root:root 0660`, so the group is `0`. On a host whose socket is `root:docker`, use that numeric gid;
  `stat -c %g /var/run/docker.sock` answers in both cases.
- **The schema tool is pinned too.** Without `pgschema` 1.13.4 on `PATH` every `api_*_e2e` case fails
  with `apply-plan failed — the committed plan in deploy/plan was rendered by 1.13.4, and a different
  version renders a different plan`, and that message points at `docs/dependencies.md`. The digest to
  check against is the one `deploy/schema/Dockerfile` builds the deploy image from.
- **`/src` must belong to 1000.** The root phase runs `apt-get` as root; anything it creates under the
  tree (a log directory, for instance) would be refused to the gate afterwards. Chown the tree after
  the install, in the same container.
- **The vcpkg tree must be one this image built.** vcpkg's ABI hash mixes the compiler, and a tree
  built by a host compiler links symbols the image's glibc may not export (`libcurl` built on a newer
  host references `memset_explicit`). Mount a cache the image produced.
- **The tree path must stay `/src`.** CMake records the configure directory in its cache; a tree
  configured at another path is refused as a moved tree rather than rebuilt.
- **The sanitizer build tree is the disk cost.** The build tree plus the analysis database is several
  gigabytes; a machine that runs out mid-build fails as `No space left on device`, not as a finding.
- **`build:css` needs `bunx`, and the image links only `bun`.** `package.json` builds the published
  stylesheet with `bunx @tailwindcss/cli`, and the image's last bun step writes one symlink. Without
  `/usr/local/bin/bunx` the asset step dies with `bunx: command not found` (exit 127), `app.css` is
  never written, and `api_render_e2e` then fails three assertions about `/static/app.css` — a red gate
  that says nothing about the C++ half.
- **The published static asset is generated, not committed.** `web/public/app.css` comes from
  `bun install --frozen-lockfile && bun run build:css`; a tree taken from `git archive` does not carry
  it.

## What it produced on the runner

Measured on the runner (`m5`), 2026-11-22, image built from this tree's `deploy/runner/Dockerfile`,
from the tree at `a9738df`:

| Fact | Value |
|---|---|
| the command | `bun run check:cpp` → `tools/gate-lock.sh` → `tools/cpp/gate.ts` |
| the user the gate ran as | `runner`, uid 1000, gid 1000, groups `1000 0` |
| the verdict | `cpp gate — 59 target(s) built · 217 case(s) passed · 147 unit(s) analysed in 4 worker(s) in 724 s · 0 finding(s)` |
| the tools it printed | `g++ (GCC) 16.2.0` / `Debian clang version 23.1.6` / `Debian LLVM version 23.1.6` |
| the services the cases used | PostgreSQL 17.12 (server and client, installed in the root phase), Docker 28.5.1 through the mounted socket, `pgschema` 1.13.4 for every `api_*_e2e` schema apply |
| skips | the Catch2 live-database cases, printed by name on the verdict line as `(CMSLEAN_TEST_DSN is not set)` |
| the gate's exit | 0 |

The first run of the recipe (at `d01f0ae`) reported four findings in `generator/` —
`certify_checks.cpp:147` and `certify.cpp:789` `performance-unnecessary-value-param`,
`certify_checks.cpp:149` and `certify.cpp:791` `performance-move-const-arg` — the same four the
analyser reported natively, which is how the static half came to be run inside the image at all. They
were fixed in the source, and the analysis has reported zero since.

Two commits in between were red before the analysis: `api_settings_route_e2e` and `api_model_admin_e2e`
failed on every host at `df63a7a` (the settings page's effect sentence and the model page's form
tokens), so the gate stopped at ctest. Their fix is part of the 217 above, which is why the count is
217 rather than the 215 those two cases were missing from.

## What a write outside the named paths does

The image names what the gate may write: the caches under `/home/runner/.cache` and the build tree
inside the mounted checkout. Everything else is refused, checked in the same container as the gate's
own user:

| Path | Result |
|---|---|
| `/home/runner/.cache/tmp`, `/home/runner/.cache/ccache`, `/home/runner/.cache/bun` | written |
| `<tree>/build` | written |
| `/usr/local/bin`, `/etc`, `/opt`, `/root`, `/usr/lib`, `/home`, `/var/lib`, `/srv` | refused (`EACCES`) |

The refusals are what makes the analyser safe to point at a checkout: the gate executes code from the
tree it judges, and a user that can rewrite `/usr/local/bin` or `/etc` can take the image with it.

## Cases that do not run

A case that cannot start its cluster exits 77 with the missing tool named (`skipped: initdb is not
installed`), and ctest records it `Skipped`. The gate tolerates a skip **only** for the live database,
prints every skip by name on the verdict line, and never for the store proof: if
`api_object_readers_e2e` does not run, the gate fails by name rather than passing quietly.

A run with `CMSLEAN_TEST_DSN` unset therefore ends with its live-database cases listed as
`skipped: <name> (CMSLEAN_TEST_DSN is not set)` — visible, and not counted as a pass. Setting it needs
a server with the plan applied; that is the deployment's job (`deploy/`), not this recipe's.
