# The runner image

Every tool the C++ gate runs, at one recorded version, in one image (ADR 0043 §2, §6). A green `check:cpp` from this
image is a claim about a toolchain, not about whichever analyser the host happened to have.

| Tool | Version | Where the pin lives |
|---|---|---|
| GCC | 16.2.0 | the base image, named by digest in the `FROM` line |
| clang-tidy | 23.1.6 | the exact LLVM package installed below |
| clang (compile database) | 23.1.6 | the same package set, symlinked for the analyser |
| cmake, ninja, make, the ports' build tools | Debian 13 (trixie) | the second `apt-get install` |

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
docker run --rm -v "$PWD:/src" -w /src \
  -e VCPKG_ROOT=/src/build/vcpkg -e CCACHE_DIR=/src/build/ccache \
  cms-runner:gcc16 bash -lc 'cmake --preset safety && bun run check:cpp'
```

The image says what it carries before it judges anything: `RUN clang-tidy --version && g++ --version` is the last layer,
so the analyser's version is in the build log of every image ever produced.
