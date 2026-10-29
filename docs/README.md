# Documentation

Read in this order. Everything here is published; the reasoning behind a rejected option is not, and
lives in `.agents/` on the machine that made the decision.

| You want to know | Read |
|---|---|
| What this project is, and what actually works today | [`../README.md`](../README.md) |
| What is being built, in what order, and how each step is proven | [`plan.md`](plan.md) |
| Which packages and images are used, at which version and licence | [`dependencies.md`](dependencies.md) |
| What was decided, and what it costs | [`decisions/`](decisions/) |
| The domain: topics, modules, tracks, tasks, lifecycle | [`content-model.md`](content-model.md) |
| The database: tables, and the invariants it enforces | [`schema.md`](schema.md) |
| The server: sizing, exposure, threat table, operator checklist | [`infra/single-server.md`](infra/single-server.md) |
| Which features are shipped versus planned | [`../FEATURES.md`](../FEATURES.md) |
| The rules an agent or contributor must follow | [`../AGENTS.md`](../AGENTS.md) |

## Decisions

| ADR | Covers |
|---|---|
| [0001](decisions/0001-scope-stack-sandbox.md) | judging on `isolate`, repo topology |
| [0002](decisions/0002-theme-and-auth.md) | `judge mono` tokens, Thai rules, account state machine, CSS |
| [0003](decisions/0003-infrastructure-and-security.md) | the twelve deployment decisions and what was verified |
| [0004](decisions/0004-language-config-authority.md) | the language matrix, configuration as data, authority, schema ownership |

A superseded heading stays in its ADR with a pointer to the document that replaced it, so a reader
following an old link is told, not misled.
