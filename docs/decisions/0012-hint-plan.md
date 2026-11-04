# 0012 — Hints are planned on first request, then cached

Status: accepted 2026-11-04 · decision by the operator

## 1. Shape

A hint is a set of sections, revealed one at a time in a side panel. The learner presses the hint
button, reads one section, presses continue for the next. Each section costs 1 token, charged on
reveal.

## 2. Who decides how many sections

The first request for a task asks the model to decide the plan: how many sections this task deserves
and what each one covers. The number must fall in **2–5**; a reply outside that range is rejected and
asked for again.

Why the model and not the task: how much help a task needs is a property of the task, and the system
has no rule that could predict it. Why a range rather than a free choice: a learner pays per section,
so an unbounded plan could cost more than creating a new task.

## 3. Cache

The plan and each section's text are stored against the task. A second learner asking for hints on the
same task reads what the first request produced — the plan is not re-decided and the text is not
rewritten. Caching is what makes an on-demand feature affordable and keeps two learners from seeing
different hints for the same task.

## 4. Failure

A request may fail three ways: the plan falls outside 2–5, a section ignores the task, or the call
fails. The request retries up to **3 times** inside the same call. If it still fails, the token is
returned, the learner is told to try again later, and the reason is recorded.

Returning the token is deliberate: the learner paid for help, not for the system's failure.

## 5. Not approved, not checked by the pipeline

Hints do not pass the task-quality checks and no administrator approves them. They are model output
shown directly. This is the one place in the system where generated text reaches a learner without a
gate, and it is recorded here so nobody later assumes otherwise.
