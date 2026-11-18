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
