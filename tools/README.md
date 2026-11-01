# Tools — one job per language

Thirty-nine files under `tools/`: thirty shell, nine TypeScript. C++ owns a fourth kind of work and
owns no file here. The three rows below say which language does which job, and every file in the
inventory maps to exactly one of them.

## The rule

| Row | Language | Job |
|---|---|---|
| Judgement | C++ | Decides whether a student's program is correct. |
| Orchestration | TypeScript | Runs a tool, parses what it printed, and reports it. |
| Assertion | Shell | Decides whether a fact about the files and containers on this machine holds. |

A tool that needs two of these jobs is two tools.

**Judgement never leaves C++.** A verdict is produced by `testlib` and by nothing above it:
`judge/checkers/token.cpp` is the token checker, `content/tasks/<slug>/solutions/*.cpp` are the
reference solutions the examples are generated from, and `judge/src/supervisor.cpp`,
`judge/src/verdict.cpp` and `judge/src/verify.cpp` run and compare. No TypeScript and no shell
re-derives a verdict; they may only read one that C++ produced. None of these files live under
`tools/`, which is why the inventory below has no C++ row.

**The shell/TypeScript line is the shape of the fact, not the size of the check.** Shell asserts a
fact the shell can express directly — a file exists, two files are byte-identical, a lockfile is
absent, a container publishes nothing on a public interface, a command exited non-zero. TypeScript
asserts a fact that needs a schema, a graph, or a foreign format parsed into types: a task against
`content/schema/task.schema.json`, the tag vocabulary as a directed graph, a `pgschema` plan document,
a clang-tidy diagnostic. `vocab/gate.ts` asserts facts about files, and it is TypeScript because those
facts are graph properties over `seed/vocab/vocabulary.json`, not lines a `grep` can count.

Inside each row there are two shapes, and they are named in the inventory:

- A **gate** exits non-zero when its fact fails to hold, and names the fact.
- A **driver** performs the single mutation whose result a gate then asserts, or that production runs
  directly. `apply-plan.sh` applies a reviewed schema plan after checking its digest; `render-tables.sh`
  prints the header `check-db.sh` compares byte for byte; `render-assets.sh` turns SVG masters into
  rasters; `upgrade.sh` deploys the release `release.toml` names.

## Inventory

Measured with `wc -l` over `find tools -type f`. The `tools/README.md` this table lives in is not a
tool and is not counted.

| File | Language | Lines | Row | Shape |
|---|---|---|---|---|
| `tools/apply-plan.sh` | shell | 32 | assertion | driver — checks the plan digest, then applies it through pgschema |
| `tools/check-cli.sh` | shell | 44 | assertion | gate — `cmsctl`'s dependency list cannot reach the database |
| `tools/check-content-db.sh` | shell | 160 | assertion | gate — seeds the bank twice into a scratch cluster and requires the second seed to write nothing |
| `tools/check-db.sh` | shell | 110 | assertion | gate — `db/include/cms/db/tables.hpp` is byte-identical to a fresh render |
| `tools/check-deploy.sh` | shell | 149 | assertion | gate — compose exposure, isolation, pinning, Caddyfile, Prometheus |
| `tools/check-island-fallback.sh` | shell | 32 | assertion | gate — the submission form posts without the editor island |
| `tools/check-pm.sh` | shell | 57 | assertion | gate — exactly one lockfile, and it is `bun.lock` |
| `tools/check-references.sh` | shell | 82 | assertion | gate — every reference builds at `-O2` under `-Werror` and reproduces its examples |
| `tools/check-scaffold.sh` | shell | 138 | assertion | gate — headers compile standalone, manifests match overlay ports, then the defect is planted |
| `tools/check-schema.sh` | shell | 85 | assertion | gate — `deploy/plan` renders from `deploy/schema/main.sql` and applies to no further change |
| `tools/check-settings.sh` | shell | 48 | assertion | gate — a secret setting has no read-back path in template or catalogue |
| `tools/check-templates.sh` | shell | 21 | assertion | gate — `api/templates` is what the design package renders, with no orphan template |
| `tools/check-unseen-task.sh` | shell | 134 | assertion | gate — the unseen-task query is served by `progress_account_task_idx` |
| `tools/check-upgrade.sh` | shell | 170 | assertion | gate — deploy order and health gate, with `docker`, `bun`, `curl` and `sleep` stubbed |
| `tools/check-verify.sh` | shell | 66 | assertion | gate — every task's cases are what its own reference solution produces |
| `tools/compare-plan.ts` | typescript | 122 | orchestration | parses two `pgschema` plan documents and names the first field that moved |
| `tools/cpp/diagnostics.ts` | typescript | 92 | orchestration | parses GCC and clang-tidy text diagnostics into typed findings |
| `tools/cpp/falsify.ts` | typescript | 439 | orchestration | runs the C++ gate against staged trees, one planted defect per case |
| `tools/cpp/gate.ts` | typescript | 361 | orchestration | drives cmake, ctest and clang-tidy, then reports what they said |
| `tools/falsify-content-gate.sh` | shell | 128 | assertion | gate — the content gate goes red on a broken bank |
| `tools/falsify-deploy-gate.sh` | shell | 93 | assertion | gate — the deployment contract goes red on a broken compose file |
| `tools/falsify-island-gate.sh` | shell | 51 | assertion | gate — the island-fallback gate goes red on a form that cannot post |
| `tools/falsify-pm-gate.sh` | shell | 73 | assertion | gate — the package-manager gate goes red on a second lockfile |
| `tools/falsify-references-gate.sh` | shell | 49 | assertion | gate — the reference gate goes red on a broken reference |
| `tools/falsify-schema-gate.sh` | shell | 92 | assertion | gate — the schema gate goes red on a hand-edited plan |
| `tools/falsify-security-gate.sh` | shell | 68 | assertion | gate — the scan goes red on a planted secret, CVE and hardening breach |
| `tools/falsify-settings-gate.sh` | shell | 56 | assertion | gate — the settings gate goes red on a secret that can be read back |
| `tools/falsify-stack-gate.sh` | shell | 88 | assertion | gate — the stack gate goes red on a broken `release.toml` |
| `tools/falsify-templates-gate.sh` | shell | 46 | assertion | gate — the template gate goes red on a stale or orphaned template |
| `tools/falsify-upgrade-gate.sh` | shell | 89 | assertion | gate — the upgrade gate goes red on a reordered deploy step |
| `tools/port-templates.ts` | typescript | 59 | orchestration | renders the design package into `api/templates` |
| `tools/render-assets.sh` | shell | 99 | assertion | driver — renders rasters from the SVG masters, with a `--check` comparison |
| `tools/render-stack.ts` | typescript | 223 | orchestration | renders `deploy/docker-compose.yml` from `release.toml` and the stack template |
| `tools/render-tables.sh` | shell | 188 | assertion | driver — prints `db/include/cms/db/tables.hpp` from `information_schema.columns` |
| `tools/security-scan.sh` | shell | 52 | assertion | gate — Trivy for secrets and fixable CVEs, plus deployment hardening rules |
| `tools/upgrade.sh` | shell | 105 | assertion | driver — deploys the named release, restoring the previous stack when a step fails |
| `tools/validate-content.ts` | typescript | 291 | orchestration | compiles `content/schema/*.schema.json` with `ajv` and prints the counts it scanned |
| `tools/vocab/build.ts` | typescript | 96 | orchestration | rebuilds `seed/vocab/vocabulary.json` from the raw syllabus evidence |
| `tools/vocab/gate.ts` | typescript | 235 | orchestration | reads the vocabulary as a graph and reports unresolved ids, duplicate meanings and loops |

| Language | Files | Lines |
|---|---|---|
| shell | 30 | 2 605 |
| typescript | 9 | 1 918 |
| total | 39 | 4 523 |

## Gates and falsifiers

A gate and a falsifier are one unit of evidence. The gate asserts a fact; the falsifier plants the
defect the gate claims to catch and requires the gate to go red **with the message that names the
defect** — a crash also exits non-zero, and a crash proves nothing. A gate without a falsifier is not
done, and a falsification case is never weakened to reach green. `AGENTS.md` states the rule under
"Gates" and under "Never"; this section records which pairs exist.

Twelve gates have a falsifier of their own, all reachable from `package.json`:

| Gate | Falsifier |
|---|---|
| `validate` | `validate:falsify` |
| `check:schema` | `check:schema:falsify` |
| `check:references` | `check:references:falsify` |
| `check:stack` | `check:stack:falsify` |
| `check:deploy` | `check:deploy:falsify` |
| `check:upgrade` | `check:upgrade:falsify` |
| `check:security` | `check:security:falsify` |
| `check:cpp` | `check:cpp:falsify` |
| `check:pm` | `check:pm:falsify` |
| `check:templates` | `falsify:templates` |
| `check:island` | `falsify:island` |
| `check:settings` | `falsify:settings` |

The last three break the naming pattern rather than the pairing: the gate is a `check:` and the
falsifier is a `falsify:`, because `templates`, `island` and `settings` do not have a colon-separated
area to nest under.

Four more gates carry their falsification inside the gate file itself, so they need no second script:
`check-scaffold.sh` plants the header defect at the end of its own run, `check-verify.sh` points the
verifier at a reference that prints nothing, `check-content-db.sh` requires its second seed to write
nothing, and `check-unseen-task.sh` drops `progress_account_task_idx` and requires a sequential scan.

Three gate scripts have no falsification at all, in a script or in the file:

| Gate | Script |
|---|---|
| `check:cli` | `tools/check-cli.sh` |
| `check:db` | `tools/check-db.sh` |
| `vocab:gate` | `tools/vocab/gate.ts` |

They are green on the tree and they have never been shown red. By the rule above they are not done.
Closing that gap is adding the case, not adding a row to this document.

## Two gates that stay out of C++

"Do not port a gate to C++" returns often enough that the reasons belong in writing. Both are about
what the port would have to own.

**`validate-content.ts` in C++ would need a hand-rolled JSON Schema engine.** The gate's whole job is
`ajv.compile()` against `content/schema/task.schema.json` and `content/schema/tracks.schema.json`
(`tools/validate-content.ts:35`, `:61`, `:210`), configured `strict` and `allErrors`. Those schemas use
`definitions`, `$ref`, `pattern`, `multipleOf`, `minProperties`, `additionalProperties: false`, `enum`,
`items`, `minItems`, `minLength`, `minimum`, `maximum` and `required`. A C++ port means implementing
that subset plus `allErrors` reporting, and every keyword it implements wrongly makes the gate accept a
bank it should reject — a validator that silently under-checks is worse than no validator, because the
green run is what the reader trusts.

**`tools/cpp/gate.ts` in C++ would have to compile itself with the toolchain whose breakage it exists
to detect.** The gate's product is a report on the C++ toolchain: `CMakePresets.json` owns the flags,
`ctest` owns pass/fail per example, `clang-tidy` owns the defect classes a sanitizer cannot see, and
`tools/cpp/gate.ts` reports what they said (`docs/decisions/0006-language-matrix.md`, invariant 5:
"`tools/cpp/gate.ts` only reports what those tools said"). Written in C++, the reporter would be a
translation unit that must be compiled before it can report, by the compiler it is meant to be
checking — and `cmake/ProjectSettings.cmake:22` refuses to configure under anything but GNU g++ 16 or
newer. When that compiler is the thing that broke, the gate and its report go down together and nothing
prints a red line. Running under `bun` instead keeps the reporter independent of the toolchain it
reports on, which is the property that makes its output worth reading.

## Adding a tool

A new gate goes in the language that owns its job, with its falsifier in the same commit. A gate that
arrives without the case that proves it can fail is a gate that asserts nothing.

Adding a new dependency group to the TypeScript row is a decision for `docs/decisions/`, not for a
`package.json` edit: `ajv` is there because the content gate validates a schema, not because JSON is
convenient to read.
