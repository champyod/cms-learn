# 0009 — How the web UI is authored

**Status:** proposed · **Date:** 2026-11-01 · **Amends:** 0006 (its "author the UI directly in the repo" row), keeps everything else in 0006 standing

## Context

ADR 0006 chose `C++26 + Drogon + Inja 3.5.0 + htmx 4.0.0`, server-rendered, and rejected the TypeScript
Next.js web with a reason that still holds: the admin panel's React components were never reusable, only the
design tokens were. `docs/plan.md` E6 states the constraint the choice exists to satisfy — *"no Node process
in production"*.

What 0006 did not say is **where the layout is designed**. Authoring HTML by hand in `api/templates/` is one
answer; it is not the only one, and it puts the design work in the place with the worst feedback loop.

The question that decides this is what survives a port from a React component to a server-rendered template.

## What ports, and what does not

| Feature | Ports to server-rendered Inja | Why |
|---|---|---|
| layout, type scale, spacing, colour | yes | they are tokens and markup, not state |
| lists, tables, cards, empty and error states | yes | rendered once from data the handler already has |
| forms, validation messages, redirects | yes | a round trip is the interaction |
| partial updates, polling, infinite scroll | yes, with htmx | swap an element for a fragment the server renders |
| live preview while typing, multi-step wizards, drag and drop, offline | no | these need client state that outlives a request |

The last row is the only real gap, and 0006 already accepted exactly one instance of it: the CodeMirror
editor island for the task page. That is the precedent — **an island is allowed where client state is the
requirement, and nowhere else.**

## Decision

1. **React is the design layer, not a shipped layer.** A design package holds the components and a rendered
   gallery, so layout and interaction are designed and reviewed where they can be seen and changed quickly.
2. **The shipped artifact is Inja templates plus one token stylesheet**, generated from that package by a
   deterministic step that runs in CI and whose output is committed. A template that is not the output of the
   port step fails the gate; the port cannot rot silently.
3. **The C++ engine ships.** One binary, server-rendered HTML, no Node at runtime. htmx carries partial
   updates. TypeScript in production is limited to the island budget in point 4.
4. **Island budget: one, CodeMirror 6**, as 0006 already decided. Adding a second island is a new ADR, not a
   judgement call inside a feature branch.
5. **The snapshot tests render the shipped Inja templates**, never the React components, so what is tested is
   what a learner receives.

## Consequences

- Design iteration happens in React; production remains one process with no client framework to hydrate.
- The port step is a build dependency: it must be deterministic, gated, and fail when a component has no
  template counterpart. That is the price of the 0006 constraint, and it is smaller than maintaining two
  implementations by hand.
- Anything that genuinely needs client state is either a round trip or the one island. If a screen cannot be
  built that way, the screen is wrong for this architecture — that is a design constraint, not a missing
  feature.
- 0006's table stays as it is: the api+web+admin row is still C++26 with Drogon, Inja and htmx. What changes
  is only where the markup is authored.
