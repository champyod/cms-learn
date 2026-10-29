# 0007 — Tag vocabulary: four syllabuses, one hierarchical tree

**Status:** accepted · **Date:** 2026-10-29 · **Supersedes:** the flat 33-entry `content/topics.json` list as the *shape* of the vocabulary (the ids themselves survive)

## Context

`content/topics.json` was 33 hand-written labels. Three problems followed from that. A task could be
tagged `geometry` and nothing narrower, so a filter could not separate a convex-hull problem from a
point-in-polygon one. The labels had no relation to each other, so nothing could say that binary
search is a divide-and-conquer technique. And the list was not traceable to anything: no reviewer
could tell whether `offline` was in scope because a syllabus said so or because someone needed it
that day.

The platform teaches to four published syllabuses — IOI, USACO, the Thai POSN curriculum and the
Codeforces tag set — and a student arriving from any of them expects to find their own vocabulary
here, not a translation of someone else's.

## Decision

1. **The four syllabuses are recorded verbatim first.** `seed/vocab/raw/{ioi,usaco,posn,codeforces}.json`
   hold every statement as the source published it, with the URL, the fetch date, the source's own
   grouping and the source's own total (233 / 149 / 108 / 38). They are evidence; they are never read
   by a service.
2. **One node per skill; other vocabularies become aliases.** A node is `{id, en, th, parent,
   aliases[{name, source, section}], sources[]}`. The same skill named four ways is one node with
   four aliases, so a task tagged `binary-search` is findable from `binary search`, `Binary Search`,
   `การค้นหาแบบทวิภาค` and `binary search และ sequential search`.
3. **The hierarchy separates a paradigm from its techniques.** `divide-and-conquer` has
   `binary-search`, `ternary-search`, `quickselect`, `merge-sort`, `dc-dp` and `dc-srq` under it;
   `dp` has nine DP variants; `graph-traversal` has `bfs`, `dfs`, `flood-fill`, `tree-traversal`. A
   problem may be tagged at either level.
4. **`seed/vocab.json` is generated, never edited.** `tools/vocab/build.ts` rebuilds it from the
   raw files plus `seed/vocab/mapping.json` (a data file: one target or one drop reason per raw
   statement). Running it twice produces a byte-identical file, so a source update is a rebuild.
5. **The 33 existing ids are node ids.** Every id in `topics.json` still resolves, so no stored task
   or module reference breaks and nothing has to be migrated.
6. **Thai is never invented.** A node's Thai name is either the Thai wording of the POSN row that
   maps to it, or the Thai name the id already had, and `th_source` records which. A node with
   neither gets `th: null` (132 of 191 nodes today, counted by the gate)
   today) instead of shipping an English label as if it were Thai.
7. **`tools/vocab/gate.ts` is the checker.** It fails when a raw statement is neither mapped nor
   dropped, when a statement maps to two nodes, when an alias repeats or means two nodes, when the
   parent graph has a cycle or a missing parent, when a node has no English name, when a Thai name
   has no provenance, when `vocab.json` is stale against the mapping, or when a stored topic id in
   `topics.json` or `modules.json` no longer resolves.

What is dropped, and why: statements whose subject is how to use a tool, a guide, a community or a
language's plumbing cannot describe what a problem requires, so they are recorded in `dropped[]`
with a reason rather than mapped. 48 IOI statements (all of SE1..SE12), 31 USACO modules (guide,
practice and C++-tooling pages), 13 POSN rows (compiler/grader usage) and the Codeforces `*special`
pseudo-tag.

## Rejected

- **A flat union of the four lists (~530 tags).** No deduplication, no parent, and the same skill
  appears four times with four spellings — the exact problem the change exists to solve.
- **Per-source vocabularies with a translation table.** Every consumer would have to join four
  vocabularies to answer "what is this task about", and the join is where the drift hides.
- **Keeping `topics.json` as the vocabulary and adding `parent`/`aliases` to it.** It would make the
  file both the generated artifact and the hand-edited source, so a rebuild would fight the editor.
  `topics.json` stays as the 33-id compatibility record; `vocab.json` is the vocabulary.
- **Auto-translating Thai labels.** A machine translation of `Convex hull` is a claim about the
  curriculum that no source made. The review list is the honest output.
- **Treating "not in the IOI syllabus" as "drop".** IOI classifies topics as excluded; a problem can
  still require them (and POSN teaches some). Only tool/guide/community/plumbing entries are dropped.

## Consequences

- Adding a tag means editing `seed/vocab/mapping.json` and rebuilding, not editing JSON output.
- A syllabus update means replacing one raw file and re-running the gate; the gate names every
  statement that lost its mapping.
- 132 nodes are English-only today. That number is visible in the gate output, and each id is in
  the count in the gate output, so the number can only go down deliberately.

## Thai coverage (decided 2026-10-29)

59 of 191 nodes carry a Thai name from the POSN syllabus or from the 33 original topics. The other
132 have none, and none was invented. The operator chose to show the English name alone for those
nodes rather than translate them ahead of use: a Thai label is added when a source supplies one, and
the interface shows a single name rather than mixing languages mid-list. There is no separate list of untranslated nodes: `th: null` in
`seed/vocab.json` is the record and the gate prints the count, so nothing has to be kept in step.
Adding a Thai name is a data change in `mapping.json`, not a code change.

## How the seed meets the database (decided 2026-10-29)

The seed is applied on every deploy, insert-only: a node whose id is not in the database is added,
and an existing row is never overwritten. The database therefore owns every name an admin has
edited, while a term added to the seed later still arrives without an admin recreating it by hand.
The key is the node id, which the mapping owns and which is never renamed; a node removed from the
mapping is not deleted from a live database, because problems already carry it.

The apply step itself is not built yet: it belongs with the deploy path, whose migration runner is
outside this lane.
