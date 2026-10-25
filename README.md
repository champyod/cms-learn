# cms-learn

A learning platform for competitive programming, run beside the contest stack (`cms-docker`)
rather than inside it: one task bank, a per-account curriculum path, and its own judge.

Sibling naming on purpose — `cms-learn` sorts next to `cms-*` in the hub and reads as the
learning arm of the same family.

## What it is not

- Not a contest system. Contests stay on `cms.mwit.ac.th`; nothing here reads or writes contest data.
- Not a general LMS. One subject (competitive programming), one audience (30-60 students), TH/EN.
- Not an AI-content farm. Every task that a student can be served has passed a machine
  verification gate first (see `docs/content-model.md`).

## Layout

```
api/        Rust service — HTTP API, auth, progress, task serving
judge/      C++ core — sandbox launcher (isolate), checker (testlib), limits
web/        TypeScript UI — Next.js, tokens shared with the admin panel
content/    the task bank and the curriculum tree (data, not code)
tools/      repo gates (content validation first)
docs/       decisions and models
```

`content/` is the single source of truth: the curriculum, the topic vocabulary and the tasks.
Code renders and serves it; no module title, tag or limit is ever hardcoded in a service.

## Gates

```
bun install
bun run validate       # content gate: schemas, refs, cycles, verification status
```

The validator exits non-zero on any violation and prints the counts it actually scanned, so an
empty or partial bank cannot pass as coverage.

## Status

E0 (scope + content model) in progress. See `docs/decisions/0001-scope-stack-sandbox.md` for the
four decisions this repo is built on.
