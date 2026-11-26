# 0062 — A generation job is created and watched on one screen

**Status:** accepted · **Date:** 2026-11-24 · **Decides:** how an operator starts a job and follows it

## Context

The pipeline is complete and its entrance is a database row: `generation_jobs` has a state machine, seven
steps write artefacts, and a certificate ends the job. A teacher who wants a task drafted cannot use any
of it without a shell.

Two questions had to be answered before a page could exist. Who may spend model calls, and what the page
shows while a job runs.

## The decision

**§1 — the right to generate is its own capability.** The closed list gains `generation:write`. An author
who may edit a task is not thereby allowed to create one at the provider's expense, and publishing remains
`content:publish`; three separate actions, three separate bits. No role carries it until an operator grants
it, and the system account holds it like every other.

**§2 — creating a job is one POST and nothing else.** `POST /admin/generate` takes the brief and writes a
job row in `queued`, which is the only state a row may be created in (the table's trigger already refuses
the rest). A caller without `generation:write` is refused by name, and an empty brief is refused before
anything is written.

**§3 — the page says where the job is, from rows that already exist.** The seven steps are shown, and each
step's state is derived, not invented: a step is **done** when `generation_artefacts` holds a row for it, it
is **running** when the job is `running` and it is the lowest step without one, and the **refusal** is the
reason recorded when the step ended the job failed. No new column holds progress, because a second source
of truth for it would eventually disagree with the artefacts.

**§4 — the panel is pushed, not polled.** One server-sent stream per open page carries the job's state and
its step marks; it closes after twenty seconds and the browser reopens it, and a session may hold two at
once — the same shape the verdict panel uses, because two live mechanisms in one product is one too many.

The generation stream needs **no listener**: a verdict arrives from the judge on another connection, which
is why that panel subscribes to a notification, while a job's progress is a set of rows this service can
read. So one connection reads the job, writes one event, and closes — the browser reopening it is what
makes it live. That keeps the rule the same and removes a thread, a registry of subscribers and a class of
bug where a dropped notification leaves a page showing an old step forever.

## Rejected

**Reload the page to see progress.** Cheapest to build, and it shows a teacher stale state for exactly the
minutes they are watching, which is the moment the page exists for.

**Poll every few seconds.** The rule this project already adopted: a screen that shows something changing
uses one pushed connection, never a fixed-interval request. Polling costs a query per screen per interval
to learn that nothing changed, and multiplies traffic with the number of open screens.

**A separate job list page with a detail page.** Two pages and two queries, and still no live progress
while a step runs.

## Consequences

- `tools/check-deploy.sh` gains the assertion that the stream exists and that the create route refuses a
  caller without the capability.
- The page is an admin page: it lives under `/admin`, renders from the design package like every other, and
  the content policy applies to it unchanged — no inline script.
- A job created here is indistinguishable from one created by a script: same table, same trigger, same
  certificate.
