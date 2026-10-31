# AGENTS.md — cms-learn

Repo-level instructions for any agent working in this checkout. Read the hub-root `AGENTS.md` too:
this file only adds what is specific to `cms-learn`.

## What this repo is

A learning platform for competitive programming: one shared task bank, two student-chosen paths
(USACO ladder, IOI path), its own `isolate`-based judge. It is **not** the contest system — nothing
here reads or writes contest data.

`content/` is the single source of truth. Services render it; none of them may hardcode a module, a
topic tag, a track order, a limit, or a string that already exists in the content model.

## Gates — run before every commit that touches content, and before claiming any lane green

```bash
bun install                # once, or after package.json changes
bun run validate           # content integrity; prints the counts it scanned
bun run validate:falsify   # proves the content gate can fail
bun run check:references   # every reference builds under -Werror and passes its own examples
bun run check:references:falsify
bun run check:deploy       # compose contract: exposure, isolation, pinning, Caddyfile, Prometheus
bun run check:deploy:falsify
bun run check:cpp          # CMake presets build under ASan+UBSan, ctest runs the examples, clang-tidy analyses
bun run check:cpp:falsify  # proves the gate catches a use-after-free, an overflow, a warning and a tidy-only defect
bun run gate               # content + references, in that order — this is what CI runs
bun run gate:all           # the above plus the deployment contract (needs docker) and C++ safety
```

A green `validate` with no falsification run is half the evidence. If you teach a gate a new defect
class, add its falsification case in the same commit — the harness is what proves the rule bites.

A reference solution needs an example set beside it: `content/tasks/<slug>/examples/N.in` and `N.out`.
`check:references` builds it at `-O2` with no sanitizers and diffs its output against those files — the
contest-like configuration, where optimisation can expose behaviour a sanitizer build masks. `check:cpp`
runs the *same* examples again through ctest, but under ASan + UBSan: same evidence, different question
("does it still compute the right answer when optimised" vs "does it stay inside memory when run").
Neither replaces the other, and a task with a reference but no examples fails both.

## Lanes in this repo

- A lane branch is `learn/<area>/<topic>` — `learn/judge/grading`, `learn/content/pipeline` — and its
  worktree lives at `<hub>/.worktrees/learn-<area>-<topic>`.
- Never use a bare `feat/` or `docs/` prefix here. This hub holds another project whose lanes are named
  that way, and a branch called `feat/task-view` in a shared listing says nothing about which project it
  belongs to.
- Cut a lane from `main`, and say the base in the lane's first commit.

## Orchestration

One deliverable is one epic; an epic is steps; a step ends in exactly one commit. Inside a step several
agents may work at once, but only across a boundary that was fixed before any of them started.

### The board

`~/.agents/agent-hub/-mnt-D-Datas-Disk-Champ-Coding-Github-cms-learn/` is the ownership board.
`CLAIMS.md` is the part that matters: before editing, claim the exact paths the step will touch, and
release them when it lands. A path held `active` by a different `Run` is live work — report the
conflict, never resolve it by editing first. `DECISIONS.md` holds the patterns every parallel agent
must follow; `STATE.md` says what is in flight.

### Contract before fan-out

A step with more than one writer starts with main freezing the contract: the headers, the schema, the
route shapes, the settings keys. Each dispatch then carries that contract plus its own path list, and
one instruction that outranks the task — **if the contract has to change, stop and report**, never edit
a shared header so that your slice compiles.

### Perspectives before the writer

Every step that is not a mechanical edit gets at least two independent read-only perspectives before
implementation, answering different questions: what is wrong with the plan, what evidence contradicts
it, what the reader will actually see. They run in parallel, and their disagreement is recorded in the
step's decision rather than averaged away.

### The production contract

`production` is the only writer, and its dispatch is the narrowest in the system:

| Field | Content |
|---|---|
| directory | the absolute path of this checkout |
| unit | one step, and the epic it belongs to |
| in scope | the exact paths, matching the claim rows |
| starting state | what exists now, with `file:line` |
| target state | what must exist when it returns |
| forbidden | paths outside the list, refactors, new dependencies, new settings keys |
| stop | a contract change, a missing prerequisite, an ambiguity that changes the design |
| proof | the command that must exit zero, and its output |

It returns that output. A summary of what it believes it did is not evidence.

### The join

Main integrates, runs the step gate once, and commits once. `checker` then walks the contracts in both
directions — does the higher layer provide exactly what the lower consumes, and does the lower handle
every state the higher can produce. A step whose delivered approach differs from the approved one is a
finding, not a variation.

## Adding work

- One intent per commit. The message carries the **why** and, on a branch's first commit, the base it
  was cut from.
- A change to the content model goes in `docs/decisions/NNNN-<slug>.md` **in the same commit** as the
  code, with what was chosen, why, and what was rejected. A model change with no ADR is a change the
  next reader will undo.
- Status and counts live in the hub's `epics/003-cms-learn.md` and in `FEATURES.md`. When a feature
  lands, move its row — do not start a parallel list.

## Code style

- **Symbols, not keywords, for operators.** Write `&&`, `||`, `!`, `^`, `&`, `|`, `~` — never
  `and`, `or`, `not`, `xor`, `bitand`, `bitor`, `compl`. Both forms are valid C++, so this is a
  readability choice, and a rule applied to half the tree is worse than either style applied to all of
  it: the whole tree follows it, and so does any generator that emits code.
- The rule is about operators. English prose in a comment or a message stays prose — `"a stale or
  repeated snapshot is refused"` is a sentence, not an operator.

## Invariants (do not "fix" these)

1. **No level on a task or a tag.** A route is stages of tags with a difficulty band; a task carries `topics` + `difficulty`.
   Putting a level back makes one task rank differently per track and duplicates the corpus.
2. **Reachability is checked.** A task no track stage can serve fails the gate. That is the point,
   not a bug to work around.
3. **`published` requires machine evidence.** Never hand-edit a task to `published`, and never
   weaken the verification rule to make a bank pass.
4. **One token source.** `web/tokens.css`. Tailwind or any component library maps to those
   variables; a second palette file is the duplicated-design-system defect.
5. **Thai rules:** no uppercase-as-hierarchy, no italic synthesis, body line-height ≥ 1.45, and the
   mono font is never subset by `unicode-range` (Latin would fall back to the system mono and mixed
   Thai/Latin columns drift).
6. **No fallback content.** A missing statement or an unverified task must fail loudly, never render
   a placeholder a student would read as real material.

## Never

- Invent endpoints, commands or screens. If it is not in the tree it does not exist — write "not
  built" instead of a plausible description.
- Install a host package or introduce a host environment variable in **production**. Production is
  containers only: every service, including schema apply, is an image pinned by digest, and a host
  package is a deployment decision to raise rather than an implementation detail to add.
- Commit a font, model or other vendored binary without its licence text beside it.
- Delete or weaken a falsification case to reach green.

## Where knowledge lives

| What | Where | Published |
|---|---|---|
| What the system does, and the architecture it was chosen for | `docs/` | yes |
| Why an option was compared, measured and rejected; session memos | `.agents/` | no, untracked |
| A code trap a reader could trip by simplifying | one short line in that file | yes |

The default is **no comment in code**. If the reason is architectural or behavioural it belongs in
`docs/`; only a code-level trap — one whose absence causes a defect a reader cannot infer — earns a
single short line above it, never a block, never a banner, never a narration of what the line does.

Dev tooling is the one place a native install is allowed, and it must work on Arch, Debian, Fedora,
macOS and Windows: prefer the container, discover `pacman`/`apt`/`dnf`/`brew`/`winget`/`scoop`, and
fall back to the pinned release binary for the detected OS and architecture.

## Where things live

| Path | Holds |
|---|---|
| `content/topics.json` | controlled topic vocabulary (id + TH/EN labels) |
| `content/modules.json` | areas + modules (`topics`, `prereqs`) |
| `content/tracks.json` | tracks → stages (`modules`, `difficulty` band) |
| `content/tasks/<slug>/` | `task.json`, one statement per locale, reference solution |
| `content/schema/` | JSON Schemas — strict, `additionalProperties: false` |
| `tools/validate-content.ts` | the content gate |
| `tools/falsify-content-gate.sh` | proof that the gate can fail |
| `tools/check-deploy.sh` | the deployment contract: exposure, isolation, pinning, Caddyfile, Prometheus |
| `tools/falsify-deploy-gate.sh` | proof that the deployment contract can fail (7 cases) |
| `tools/security-scan.sh` | Trivy: secrets, fixable CVEs, deployment hardening |
| `tools/falsify-security-gate.sh` | proof the scan catches planted defects (3 red, 1 clean control) |
| `core/` | the shared C++ library: capabilities, settings, schema access, errors |
| `references/` | testlib checkers and reference solutions, built and run by `check:cpp` |
| `cmake/` | compiler options and sanitizer configuration, declared once |
| `tools/cpp/gate.ts` | the C++ gate: presets, ASan + UBSan, ctest, clang-tidy |
| `tools/cpp/falsify.ts` | proof the C++ gate catches a use-after-free, an overflow, a warning and a tidy defect |
| `deploy/schema/` | the only DDL, linked by `main.sql`; `docs/schema.md` holds its invariants |
| `docs/plan.md` | the build order: epics, steps, gates |
| `docs/dependencies.md` | every package, image and tool with its version and licence |
| `deploy/` | single-server compose, Caddyfile, CrowdSec, backup + restore drill, runbook |
| `docs/infra/single-server.md` | sizing, threat table, operator checklist |
| `web/tokens.css` | design tokens (`judge mono`) |
| `design/` | the mark and its variants; `logo-brief.md` states what it carries |
| `docs/decisions/` | ADRs |
| `docs/content-model.md` | the model, the cache key, the serving policy |
| `FEATURES.md` | per-feature registry, shipped vs planned |
