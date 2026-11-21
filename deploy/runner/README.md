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

The gate also needs four things this image does not carry yet. They are mounted in the command below rather than baked
in, because they are the runner's environment and not the analyser: bun runs the gate, a vcpkg checkout supplies the
api's and judge's packages, Docker runs the store proof, and PostgreSQL backs the e2e fixtures.

| Tool | Version | Where it comes from |
|---|---|---|
| bun (the gate's entry point) | 1.4.4 | mounted at `/opt/bun`, on `PATH` |
| vcpkg (the api/judge packages) | the checkout's `vcpkg version` | mounted over `build/vcpkg`; `VCPKG_ROOT` names it |
| docker CLI (the store proof) | 27.5.1 | installed in the container; the daemon is the host's socket |
| PostgreSQL (the e2e fixtures) | 17 | installed in the container, because the gate's cases start a scratch cluster |

## Why clang-tidy is not from Debian

Debian 13 ships clang-tidy 19.0. The gate's findings were triaged against 23, and the same tree reports fourteen
pre-existing findings on 19 while 23 reports none — the analyser's check set grows between major versions, so its verdict
is evidence only at the version the findings belong to. §6 records that measurement; this image is the answer.

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
