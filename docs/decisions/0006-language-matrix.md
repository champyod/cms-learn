# 0006 — Language matrix (supersedes ADR 0001 §2)

**Status:** accepted · **Date:** 2026-10-27 · **Supersedes:** 0001 §2 (the C++/Rust/TypeScript split) · **Numbering:** 0004 and 0005 are already claimed by the unmerged `feat/content-pipeline` and `feat/task-view` branches, so this record takes 0006 to avoid a collision at merge time

## Context

ADR 0001 split the system three ways by layer: C++ judge core, Rust services, TypeScript UI. A later
design pass proposed a different matrix and left three questions open — whether C++ could replace
Atlas, whether C++ could take the judge supervisor and the shared verifier, and whether Rust could take
the content gate and the editor island. Those were answered against the actual repositories, and the
operator then chose the variant that keeps the domain logic single-sourced rather than the one that
maximises memory safety per process.

## Decision

| Layer | Language | Package | Why this one |
|---|---|---|---|
| api + web + admin UI | C++23 | Drogon 1.9.14, Inja 3.5.0, htmx 4.0.0 | one binary, one domain core, server-rendered HTML |
| domain core (curriculum graph, task schema, limits, serving rules) | C++ | one static library, two callers | api and judge must not disagree about what a task is |
| judge checkers and verdict comparison | C++ | testlib 0.9.41 + isolate v2.7 | `testlib` **is** the comparison — nothing above it re-derives a verdict |
| judge supervisor | Rust | `nix` / `rustix`, pidfd, cgroup v2 | privileged process control that parses untrusted bytes |
| `cmsctl` | Rust | no database dependency | cannot reach around the core by construction |
| content gate | C++ | calls the domain core validator | one validator, so content and runtime cannot drift |
| editor island | TypeScript | CodeMirror 6 | browser DOM; Rust here would mean wasm *and* JS |
| schema | Atlas HCL v1.3.0 → generated SQL | `atlas.sum`, `migrate validate`, `migrate lint` | declarative source, tamper-evident output |

### Rejected, with the reason

- **C++ replacing Atlas.** Atlas is a binary used by CI and deploy, not a library in the app, so
  replacing it removes no language from the product and adds a schema-diff engine we would own. No
  mature C++ equivalent exists (`sqldef`/`pg-schema-diff` are Go, `migra` is Python; Rust's
  `sqlx`/`refinery` are versioned-SQL runners without a declarative diff). Rejected as months of work
  that recreates the hand-written-migration risk Atlas removes.
- **A Rust "shared verifier".** `testlib` already performs the comparison; a second verifier either
  duplicates its semantics — the exact duplication this repo forbids — or is glue with no reason to be
  shared. Verdicts stay in `testlib`; Rust orchestrates.
- **Rust for the content gate.** The gate already works and is falsified (11 seeded defects). Porting
  it to Rust buys only language purity; pointing it at the shared C++ validator buys a single
  implementation of the rules that api, judge and gate all obey.
- **Rust for the editor island.** CodeMirror 6 is browser JavaScript. Rust means a wasm module plus the
  JS editor anyway.
- **Keeping the TypeScript Next.js web.** Cannot share the domain core with the judge, and the admin
  panel's React components were never reusable from a separate app — only the design tokens were, and
  those are already a plain CSS file.

## Invariants (a change here is a new ADR)

1. **Inja autoescape is on, always.** `html_autoescape` defaults to **false** in Inja
   (`single_include/inja/inja.hpp:922`); left off, a student's name or a statement body is stored XSS.
   The renderer sets `set_html_autoescape(true)` at construction and a test asserts a `<script>` in a
   display name comes out escaped.
2. **No `execSqlSync` in a request handler.** Drogon offers both (`execSqlSync`, `execSqlAsync`,
   `execSqlAsyncFuture`); the synchronous one blocks an event-loop thread.
3. **The admin surface is its own listener and container.** Three surfaces in one process is one blast
   radius; admin gets a separate bind address, its own session scope and its own container.
4. **Rust never computes a verdict, C++ never parses unstructured foreign output without a fuzzer.**
   The supervisor treats checker output as untrusted data with a hard size cap.
5. **Every C++ build runs under ASan + UBSan with `-Werror` and `_GLIBCXX_ASSERTIONS`, and the flags live in the build.** Because the internet-facing path is now C++, this gate is mandatory, not best-effort. `cpp/CMakePresets.json` owns the flags, `ctest` owns pass/fail per example, `clang-tidy` owns the defect classes a runtime sanitizer cannot see, and `tools/cpp/gate.ts` only reports what those tools said. A gate that re-declared its own compiler flags in a script would test a configuration nobody ships — that is why this is not a shell script.
6. **`atlas migrate validate` and `migrate lint` run in CI.** Without them `atlas.sum` is a file nobody
   checks and the "no hand edits" claim is decoration.

## Consequences

- The first schema migration is reviewed as `schema.hcl` and then generated once as `0001_init`. The
  three options presented for that decision were not mutually exclusive: reviewing the declarative
  schema first *includes* a single honest init, because the HCL is the review artifact and SQL is
  derived from it.
- `web/tokens.css` remains the design source; the `judge mono` work is unaffected because it was never
  framework-shaped.
- Component reuse with the `cms-docker` admin panel is lost. Accepted: what was genuinely reusable is
  the token file, and that survives.
- Four toolchains persist (C++23, Rust, TypeScript for the editor island, Atlas as a binary). The
  fourth is a build-time binary, not a runtime in the product.
- `docs/infra/single-server.md`, `FEATURES.md` F-034 and the proposal annex must name Drogon/Inja
  rather than Rust/Next.js, or they state something false.

## Proof obligations

| Claim | How it will be proven | State |
|---|---|---|
| C++ path is memory-safe enough to be exposed | `bun run check:cpp` — CMake presets + ctest + clang-tidy, falsified per layer (use-after-free, signed overflow, stack overflow, warning-as-error, static-analysis-only) | gate exists and is falsified 7/7; no api code yet |
| Autoescape on | test that a `<script>` in a user field renders escaped | not built |
| No sync DB in handlers | grep gate over `api/` sources for `execSqlSync` | not built |
| Content and runtime agree | gate calls the domain core validator; api calls the same | not built |
| Schema has no hand edits | `atlas migrate validate` in CI | not built |
| htmx 4.0.0 is safe to adopt (released 2026-08-28, a recent major) | a swap-and-render smoke test on the four screens before the pilot; 2.x remains the fallback and the templates are plain attributes | not built |
