# 0001 — Scope, stack, sandbox

Status: accepted 2026-10-25 · §2 superseded by 0004 · §3 superseded by `docs/content-model.md`

## 1. Judging runs here, on `isolate` in a container

A dedicated judge worker runs `isolate` inside a container with a delegated cgroup v2 path, the same
mechanism the contest stack already operates.

Why: `isolate` gives namespace, cgroup and seccomp isolation with millisecond startup, is packaged,
and is already understood by this operator. A managed sandbox would take student code out of the
school's control, and borrowing contest capacity violates the contest-live zero-downtime rule.

Consequence: the judge host delegates cgroups exactly as the CMS worker does, and nothing else is
added to the host.

## 2. One language per kind of work — superseded by 0004

C++ owns the judge core, and 0004 extends that to the api and the web layer, leaving Rust with
`cmsctl` and TypeScript with the content gate and the editor island.

The judge core is the most exposed code path, so its safety is a build configuration rather than a
hope: ASan and UBSan on every judge build, `libFuzzer` on the sandbox launcher and the checker
protocol parser, `clang-tidy` clean, `-Werror`, no raw owning pointers, no exceptions across the
sandbox boundary, and no untrusted structured data on the control plane.

## 3. Curriculum — superseded by `docs/content-model.md`

The model now lives in `docs/content-model.md`, and `AGENTS.md` invariant 1 forbids a level on a task
or a module. Track stages rank modules; tasks carry topics and difficulty.

## 4. Repo and topology

One checkout at `cms-learn`: `core/` (C++ library), `cmsctl/` (Rust CLI), `web/` (templates, tokens,
static assets), `content/` (the bank and its schemas), `deploy/` (schema, compose, Caddyfile),
`tools/` (gates).

Why: the services share one thing that must not drift — the task and curriculum schema — and splitting
them into repositories would put that contract behind a release hop with no benefit at this size. A
service can be cut out later without touching the schema.

## Open

- None from this ADR. Auth was decided in 0002, hosting and observability in 0003, the language matrix
  and schema ownership in 0004.
