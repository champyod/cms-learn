# 0001 — Scope, stack, and sandbox

Status: accepted · 2026-10-25

Decided with the operator before any code. Each row records what was chosen, the reasoning, and
what was rejected, because a later reader cannot reconstruct a decision from a diff.

## 1. Judging runs here, on `isolate` in a container

Chosen: a dedicated judge worker on a **new host**, running `isolate` inside a container with a
delegated cgroup v2 path — the same mechanism the contest stack already operates
(`cms-docker/docker-compose.worker.yml:22-64`, `cms-docker/src/Dockerfile:51-58`).

Why: `isolate` gives namespace + cgroup + seccomp isolation with millisecond startup and is
already packaged, already hardened in practice, and already understood by this operator.
Rejected: a Firecracker microVM (chosen first, then withdrawn after seeing the host cost — KVM,
patched host+guest kernel and microcode, `jailer`, boot parameters, all host-level); a managed
sandbox API (student code would leave the school's control); borrowing contest-host capacity
(violates the contest-live zero-downtime rule).

Consequence recorded: the judge host must delegate cgroups exactly as the CMS worker does. No host
package, script or environment variable is added beyond what that already requires.

## 2. C++ owns the judge core; Rust owns services; TypeScript owns the UI

> **Superseded 2026-10-27 by [`0006-language-matrix.md`](0006-language-matrix.md)** for the language
> split: api, web and admin are now C++/Drogon with the shared domain core, Rust keeps the judge
> supervisor and `cmsctl`, and the C++ safety obligation below is enforced by `bun run check:cpp`
> (CMake presets + ctest + clang-tidy, falsified per layer). The judge-core reasoning stands.

Chosen: C++ for the sandbox launcher, resource limits and checker protocol; Rust for the API,
content/pipeline and task-serving services; TypeScript (Next.js) for the web app.

Why C++ for the judge: the domain's packaged primitives are C++ — `testlib` is the standard
checker library across competitive programming, and `isolate`'s own semantics are the reference
this worker mirrors. Writing that layer in Rust would mean reimplementing `testlib`'s validators
and generators by hand.

Knowing deviation, accepted by the operator against the recommendation (Rust-only services): the
judge core is the most exposed code path in the system and C++ gives up compiler-enforced memory
safety there. The mitigation is not optional:

- ASan + UBSan on every judge build, in CI and in the worker image
- `libFuzzer` targets on the sandbox launcher and the checker protocol parser
- `clang-tidy` clean, `-Werror`, no raw owning pointers, no exceptions across the sandbox boundary
- the judge never parses untrusted bytes on the control plane: the API hands it a task id and a
  source blob, and the checker receives only files that a real run produced

## 3. Curriculum is a level tree with a tag spine (A+B)

Chosen: sections → categories → modules for navigation ("what next"), with a controlled topic
vocabulary plus a numeric difficulty as the spine of the task bank.

Why: the level tree answers the beginner's question ("what do I learn next") that a topic atlas
cannot, while tags answer the serving question ("what can this account see that it has not seen")
that a level tree cannot. Both are needed by the operator's own serving rule, so the model carries
both rather than choosing.

Rejected: pure topic atlas (no sequence), pure level tree (no serving dimensions), fixed courses
(too rigid to top up with generated tasks).

## 4. Repo and topology

Chosen: one checkout, `cms-learn`, at the hub root — `api/ judge/ web/ content/`. One schema
contract, one gate, one release.

Why: the three services share exactly one thing that must not drift (the task/curriculum schema),
and splitting them into repos would put that contract behind a release hop with no benefit at this
size. Reversible: a service can be cut into its own repo later without touching the schema.

## Open decisions

- Auth model for minors (school SSO vs local accounts) — E3, not yet decided
- Hosting provider for the judge host — E7, not yet decided
- Design direction — E6, blocked on layout by the operator's own ordering (content → layout → theme)
