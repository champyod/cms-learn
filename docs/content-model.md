# Content model

One task bank, one tag vocabulary, two routes over it. Everything the site renders comes from here;
nothing about a tag, a route, a stage or a limit lives in application code.

## Why this shape

The operator's serving rule is: *serve tasks this account has not seen; if the bank has nothing left
in that field, generate more with AI.* And the path is chosen by the student: a **USACO ladder** or
an **IOI path**.

Three requirements fall out:

- **"what do I learn next"** needs a sequence: a route, stages inside it, and a difficulty band per stage.
- **"has this account seen it"** needs dimensions that are not the route: a student stuck on `dp` needs
  more `dp` at their level, not the next item in the tree.
- **one corpus, two routes** needs the route to be a *view*. If a task belonged to a route it would
  exist twice, once per route, and the copies would drift.

A tag is the one unit that serves all three: it is what a problem is labelled with, what a student
picks when they know what they want, and what the generator is asked to write for. A route is then
only an ordered set of stages, each naming tags and a band.

There are no modules. A module was a second name for a set of tags, and two names for one thing drift
apart: the module called "Fenwick trees" and the tag called "Fenwick trees" could disagree about what
they meant, and 17 of the 28 modules already covered two tags each.

## Files

| File | Holds | Invariant |
|---|---|---|
| `content/topics.json` | the tag vocabulary (id + EN/TH labels) | every tag used anywhere is defined here, and every tag is placed by some stage |
| `content/tracks.json` | routes → stages, each naming `tags` and a `difficulty` band | stage tags resolve, no tag twice inside one stage, bands not inverted, every tag placed |
| `content/tasks/<slug>/task.json` | one task: metadata, limits, checker, verification, origin | `published` implies verification passed and test data present; the task is reachable by some stage |
| `content/tasks/<slug>/statement.<lang>.md` | the statement, one file per locale | a task ships a statement for every locale it claims |
| `content/tasks/<slug>/solutions/` | reference solution(s) used by the verifier | referenced by `verification.reference_solution` |

A tag may appear in more than one stage of a route: a skill is met again at a harder band, and that
is teaching, not duplication. What the gate refuses is the same tag twice inside one stage.

"Reachable" is checked, not assumed: a task is servable only if some stage's difficulty band contains
its difficulty **and** that stage names one of its tags. An unreachable task is a defect the gate
reports, because a task nobody can be routed to is work that silently does not exist.

## Task lifecycle

```
draft ──verify──> verified ──publish──> published
  │                  │
  └──reject──────────┴──> rejected (kept for audit, never served)
```

`draft` is what a generator produces; a draft may live in the bank and is never served. `published`
requires machine evidence, because an AI-written task with a wrong expected answer teaches the wrong
thing and looks identical to a correct one from outside.

`verification.status == "verified"` requires all of:

1. **reference-pass** — the reference solution is accepted by the checker on every test
2. **bruteforce-differential** — on small random inputs the reference agrees with a brute force
   (this is what catches a wrong checker or a mis-generated expected output)
3. **limits-measured** — the reference runs inside the declared `time_ms`/`memory_mb` on the slowest
   test with margin, so a correct-but-slow student solution is not failed by a wrong limit

The gate is enforced twice on purpose: structurally here (status, methods recorded, files present)
and behaviourally by the judge before publish. A validator cannot prove behaviour; a judge alone
cannot prove the bank has no broken entries.

## Generated content: cache key, not cache copy

```
key = sha256(
  template_version ‖ prompt_version ‖ model_id ‖ model_params ‖
  canonical_json(inputs)          # {track, stage, topic, difficulty, locale, task_format}
)
```

- Same key → the stored artifact is reused, for every account, forever. A student whose path needs
  "greedy, 1200, TH/EN" gets the cached task, not a new generation.
- A template change bumps `template_version`; a prompt tweak bumps `prompt_version`. Nothing else
  invalidates.
- The cache is content-addressed and immutable: written once, then verified or rejected, never
  edited in place. A corrected task is a new `version`, so progress rows pointing at version 1 stay
  meaningful.
- Shared across accounts by design. Per-account copies are the exact waste to avoid: N accounts
  would mean N generations of the same task.

## Serving policy

For an account on track `T`, stage `S`, asking for work:

1. unseen + published + topics of `S` + difficulty inside `S`'s band, nearest difficulty
2. unseen + published + topics of `S`, widen the band by one step
3. nothing left → generate for `(topic, band, locale)` → verify → publish → serve

"Seen" is a per-account row set (`account_id, task_id, task_version`), never a flag on the task: one
task is unseen for one account and solved by another, and a flag on the task would corrupt both.

## What is deliberately absent

- No per-page content: a route page, a task page and a review page all render from the same schema
  through shared components.
- No second difficulty vocabulary: one integer scale (`800..3500`, step 100), not a level enum plus
  a difficulty enum that can disagree.
- No fallback content: a missing statement or an unverified task fails the request loudly rather
  than degrading to a placeholder a student would read as real material.
