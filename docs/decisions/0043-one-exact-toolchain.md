# 0043 — One exact toolchain, enforced, and every build happens in it

Status: accepted 2026-11-18 · decision by the operator

## 1. What the project already locked, and the gap that remained

| Thing | Where | What it says |
|---|---|---|
| the language | `generator/CMakeLists.txt` | `cxx_std_26` |
| the compiler family and floor | `cmake/ProjectSettings.cmake:24-27` | GNU g++ **16 or newer**, with the reason written out: 14 and 15 accept `-std=c++26` and compile a smaller language |
| the exact CI image | `.github/workflows/ci.yml:194`, `:244` | `gcc:16.2.0-trixie` |
| the libraries | `api/vcpkg.json`, `vcpkg-ports/` | pinned manifests |

So the project never followed "whatever the machine has" for the language or the compiler family. What it
lacked was an exact compiler version: the floor admits both **16.2.0** (the runner) and **16.2.1** (this
laptop), and both are "valid".

That is the gap, and it matters because a generated test is stored and verified by a **digest**. A digest
is a claim about bytes. If two compilers may produce the bytes, the digest does not name one artefact —
it names one artefact per machine, under one name.

## 2. The decision

| What | The rule |
|---|---|
| the compiler | one exact version — the one the CI image names — and no other compiler builds the project |
| where a build happens | in that image, for every build, a quick local check included |
| how it is enforced | configure refuses a compiler whose version is not exactly the pinned one, and the message names both the found and the required version |
| the language | C++26, unchanged |
| the libraries | unchanged: pinned manifests, compiled by that same compiler |

## 3. Why

Pinning the floor and recording the version that ran makes the record honest and leaves the artefact
ambiguous. Pinning the exact version makes the artefact one thing, which is what a digest has to mean and
what the test writer's determinism gate (`0015` §1 row 6) needs in order to be checkable at all: the same
seed and the same compiler, anywhere, produce the same bytes.

## 4. Consequences

- Every build path becomes the same image invocation: local, CI and the operator's runner. A native
  compiler on any machine is refused rather than silently producing a different artefact.
- The image is part of the project's contract. It is named by digest beside every other pinned dependency,
  and CI, the runner and any developer command use that name.
- The dependency tree is compiled by that compiler too, so a cache built by another compiler is not
  reused as if it were the same.
- The local development loop changes: a quick check becomes a container run instead of a native one. That
  is the price of the guarantee and it was accepted deliberately.
- A future compiler upgrade is a record of its own: the version, the reason, and the re-verification of
  every digest that depends on it.

## 5. What was rejected

- **A minimum major with the version recorded per run.** Honest, and still two artefacts under one name.
- **Following whatever the machine provides.** No guarantee about the language, the compiler, or the bytes.

## 6. Amendment, 2026-11-20 — the pin covered the compiler, not the lint tool

Measured on the runner (`m5`, image `cms-runner:gcc16`) at commit `3c29295`: the image's clang-tidy is
**19.1.7**, and it reports **14 pre-existing findings** on a tree the dev host's clang-tidy **23.1.6** passes with
zero. None of the 14 is in the files that step delivered, so the tree is not worse than it was — but a CI image
built the way this one is built is **red today**, for a tool §2 never pinned.

§2 says "one exact compiler version, enforced, with every build running in the image that carries it". The
measurement shows the rule was read as *compiler* when it has to mean *every tool the gate runs*: a lint result
that changes with the lint tool's version is not evidence, it is the tool's opinion of the day.

So §2 is read as covering the whole gate toolchain — compiler, clang-tidy, cmake, ninja, and whatever the
falsifiers invoke — each at one recorded version, and the image carrying those versions is the one that runs them.
Until clang-tidy is pinned, `check:cpp` is green only on a machine whose clang-tidy happens to match the version
the findings were triaged against, which is the accident this record exists to remove.

## 7. Amendment, 2026-11-21 — the analyser is pinned in the image

§6 named the gap; this closes it. `deploy/runner/Dockerfile` builds the runner image from the digest-pinned
`gcc@sha256:07280…e4eb` base and installs clang-tidy **23.1.6** from LLVM's own repository at the exact package
`1:23.1.6~++20261116014037+5b10d2d74929-1~exp1~20261116134151.96`, symlinking it as `clang-tidy` and `clang++`. The image
was built here and answers `Debian LLVM version 23.1.6` and `g++ (GCC) 16.2.0` from a single `docker run`, so the pin and
the image agree rather than merely being written down together.

The compiler did not move: the base is still the pinned GCC 16.2.0, named by digest. The analyser is now the version the
findings belong to, and the gate's own header line — which asks `clang-tidy --version` on every run — is what tells a
reader which analyser produced a verdict.

## 8. Amendment, 2026-11-21 — the image runs the gate, not the host's tools

§7 pinned the analyser; the proof that followed showed the image still borrowed bun from the host, so "one image runs the
gate" was nearly true. `deploy/runner/Dockerfile` now installs bun **1.3.14** from its release archive, checked against the
release's own `sha256` before it is unpacked, and the image answers `bun --version` with it beside `clang-tidy --version`
and `g++ --version` in the same build layer.

What cannot live in the image is named in `deploy/runner/README.md` rather than quietly mounted: the docker CLI and
PostgreSQL's server binaries, which the store and visibility proofs use to start their own clusters — tools of the proof,
not of the analyser, and a build image that can start containers holds privilege it has no use for.
