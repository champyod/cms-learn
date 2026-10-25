# Content model

One task bank, one curriculum tree, one topic vocabulary. Everything the site renders comes from
here; nothing about a module, a tag or a limit lives in application code.

## Why this shape

The operator's serving rule is: *serve tasks this account has not seen; if the bank has nothing
left in that field, generate more with AI.*

Two requirements fall out of that sentence and they pull in opposite directions:

- **"what do I learn next"** needs a sequence, prerequisites, a path a beginner can walk.
- **"has this account seen it"** needs orthogonal dimensions — a topic and a difficulty — that are
  not the path itself, because a student stuck on `dp` needs more `dp` at their level, not the next
  module in the tree.

So the curriculum is a **level tree** (`sections → categories → modules`) and the task bank is a
**flat tagged set** (`level + topics + difficulty`). The tree gives navigation; the tags give
serving. A module never owns tasks — that would make a task unreachable from a second module and
duplicate the bank.

## Files

| File | Holds | Invariant |
|---|---|---|
| `content/topics.json` | the controlled topic vocabulary (id + EN/TH labels) | every `topics` value anywhere is a key of this file |
| `content/curriculum.tree.json` | sections → categories → modules, each with `topics` and `prereqs` | ids unique, prereqs exist, prereq graph is acyclic |
| `content/tasks/<slug>/task.json` | one task: metadata, limits, checker, verification, origin | `status: published` implies verification passed and test data present |
| `content/tasks/<slug>/statement.<lang>.md` | the statement, one file per locale | a task has a statement for every locale it claims |
| `content/tasks/<slug>/solutions/` | reference solution(s) used by the verifier | referenced by `verification.reference_solution` |

## Task lifecycle

```
draft ──verify──> verified ──publish──> published
  │                  │
  └──reject──────────┴──> rejected (kept for audit, never served)
```

`draft` is what a generator produces. A draft may exist in the bank and is never served to a
student. `published` requires machine evidence, because an AI-written task with a wrong expected
answer teaches the wrong thing and looks identical to a correct one from the outside.

The verification gate (`verification.status == "verified"`) requires all of:

1. **reference-pass** — the reference solution is accepted by the checker on every test
2. **bruteforce-differential** — on small random inputs, the reference agrees with a brute-force
   solution (this is what catches a wrong `checker` or a mis-generated expected output)
3. **limits-measured** — the reference runs inside the declared `time_ms`/`memory_mb` on the
   slowest test with margin, so a correct but slow student solution is not failed by a wrong limit

The gate is enforced twice on purpose: by the validator here (structural: status, files present)
and by the judge before publish (behavioural: the three checks above). A validator cannot prove
behaviour; a judge alone cannot prove the bank has no broken entries.

## Generated content: cache key, not cache copy

Generation is keyed, never re-rolled:

```
key = sha256(
  template_version ‖ prompt_version ‖ model_id ‖ model_params ‖
  canonical_json(inputs)          # e.g. {topic, level, difficulty, locale, task_format}
)
```

- Same key → the stored artifact is reused, for every account, forever. A student whose path needs
  "greedy, 1200, TH/EN" gets the cached task, not a new generation.
- A template change bumps `template_version` and invalidates the key deliberately; a prompt tweak
  bumps `prompt_version`. Nothing else does.
- The cache is content-addressed and immutable: an artifact is written once, verified or rejected,
  and never edited in place. A corrected task is a new version (`version: 2`), so progress rows
  pointing at `version: 1` stay meaningful.
- The cache is shared across accounts by design. Per-account copies are the exact waste the
  operator asked to avoid: N accounts would mean N generations of the same task.

## Serving policy

For an account at level `L` asking for work:

1. unseen + published + level ≤ L + same topic as the current module, nearest difficulty
2. unseen + published + level ≤ L, nearest difficulty (widen the topic)
3. nothing left → generate for `(topic, L, next difficulty)` → verify → publish → serve

"Seen" is a per-account row set (`account_id, task_id, task_version`), never a flag on the task:
one task is unseen for one account and solved by another, and a flag on the task would corrupt
both.

## What is deliberately absent

- No per-page content: a lesson page, a task page, a review page all render from the same schema
  through shared components.
- No second vocabulary: difficulty is one integer scale (`800..3500`, step 100, Codeforces-like),
  not a "level" enum plus a "difficulty" enum that can disagree.
- No fallback content: a missing statement or an unverified task fails the request loudly rather
  than degrading to a placeholder the student would read as real material.
