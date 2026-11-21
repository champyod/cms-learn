# The runner image

The C++ gate's analysis runs on one recorded toolchain in one image (ADR 0043 §2, §6): GCC, the analyser, and the
build tools. A green `check:cpp` from this image is a claim about a toolchain, not about whichever analyser the host
happened to have.

| Tool | Version | Where the pin lives |
|---|---|---|
| GCC | 16.2.0 | the base image, named by digest in the `FROM` line |
| clang-tidy | 23.1.6 | the exact LLVM package installed below |
| clang (compile database) | 23.1.6 | the same package set, symlinked for the analyser |
| cmake, ninja, make, the ports' build tools | Debian 13 (trixie) | the second `apt-get install` |
| bun (the gate scripts' runtime) | 1.3.14 | `BUN_VERSION` + `BUN_SHA256` in the `Dockerfile`, checked against the release digest before it is installed |

Two things are mounted as data rather than baked in, because they are the checkout's, not the toolchain's:

| Mount | What it is |
|---|---|
| the tree | the code under judgement |
| `$HOME/.vcpkg` over `build/vcpkg` | the package cache vcpkg has already built; `VCPKG_ROOT` names it |

## Why clang-tidy is not from Debian

Debian 13 ships clang-tidy 19.0. The gate's findings were triaged against 23, and the same tree reports fourteen
pre-existing findings on 19 while 23 reports none — the analyser's check set grows between major versions, so its verdict
is evidence only at the version the findings belong to. §6 records that measurement; this image is the answer.

## What the image does not contain, and why

| Not in the image | Why |
|---|---|
| the docker CLI | only the store proof (`tools/postgres-cluster.sh`'s container cases) needs it, and that proof runs containers itself; an image that can start containers is a much larger privilege than a build |
| PostgreSQL's server binaries | the same reason: the store and visibility gates start their own cluster and speak the protocol, so the server is the host's tool, not the analyser's |

Everything else the C++ gate runs — compiler, analyser, build tools, bun — is in the image, so a `check:cpp` from inside
it is a claim about the image and not about the host it borrowed from.

## Building it

```sh
docker build -t cms-runner:gcc16 deploy/runner
```

The build fails loudly if the pinned package is gone: `apt-get install "clang-tidy-23=${CLANG_TIDY_PACKAGE}"` refuses an
unknown version instead of silently installing a different one.

## Running the gate inside it

```sh
docker run --rm --network host \
  -v "$PWD:/src" \
  -v /path/to/vcpkg:/src/build/vcpkg \
  -v /path/to/bun:/opt/bun \
  -v /var/run/docker.sock:/var/run/docker.sock \
  -w /src \
  -e VCPKG_ROOT=/src/build/vcpkg -e CCACHE_DIR=/src/build/ccache -e PATH=/opt/bun:$PATH \
  cms-runner:gcc16 bash -lc '
    curl -fsSL https://download.docker.com/linux/static/stable/x86_64/docker-27.5.1.tgz -o /tmp/docker.tgz
    tar -xzf /tmp/docker.tgz -C /tmp && install -m 755 /tmp/docker/docker /usr/local/bin/docker
    apt-get update -qq && apt-get install -y -qq postgresql postgresql-client
    cmake --preset safety && bun run check:cpp'
```

The tree may be a Docker volume instead of a host directory: mount the volume at an absolute path in place of `$PWD`, and
remove `build/safety` and `build/tidy` when that path changes — CMake refuses a cache created at a different directory,
which is a moved tree rather than a broken one.

The store proof needs the host's Docker socket and the `chrislusf/seaweedfs:4.51` image. Without both, that case skips
and the gate fails by name rather than passing quietly.

The image says what it carries before it judges anything: `RUN clang-tidy --version && g++ --version` is the last layer,
and the gate prints the versions it ran beside its verdict, so the analyser that judged is in every log.
