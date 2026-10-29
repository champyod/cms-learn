#!/usr/bin/env bash
# Falsification for the content gate.
#
# A green run proves nothing until the same gate is shown going red on a bank that is actually
# broken. Each case is a real defect the bank can acquire, applied to a throwaway copy so the
# working tree is never touched.
#
# Usage: bash tools/falsify-content-gate.sh
set -uo pipefail

src="$(cd "$(dirname "$0")/.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

cp -a "$src/content" "$src/tools" "$src/package.json" "$work/"
ln -s "$src/node_modules" "$work/node_modules"

run_gate() { ( cd "$work" && bun run tools/validate-content.ts >/dev/null 2>"$work/err" ); }

reset_bank() { rm -rf "$work/content"; cp -a "$src/content" "$work/content"; }

pass=0
fail=0

expect() { # <name> <expected exit: 0|1>
  local name="$1" expected="$2" got
  if run_gate; then got=0; else got=1; fi
  if [ "$got" = "$expected" ]; then
    printf '  [ok]   %-46s gate exit %s (expected %s)\n' "$name" "$got" "$expected"
    pass=$((pass + 1))
  else
    printf '  [FAIL] %-46s gate exit %s (expected %s)\n' "$name" "$got" "$expected"
    sed -n '1,6p' "$work/err"
    fail=$((fail + 1))
  fi
}

edit_json() { # <file> <python expression; the document is bound to both `task` and `doc`>
  python3 - "$1" "$2" <<'PY'
import json, sys
path, expr = sys.argv[1], sys.argv[2]
with open(path) as fh:
    doc = json.load(fh)
task = doc
exec(expr)
with open(path, "w") as fh:
    json.dump(doc, fh, ensure_ascii=False, indent=2)
PY
}

echo "content gate falsification"

# 1. the honest bank passes
reset_bank
expect "baseline bank" 0

# 2. a task published with its verification still pending
reset_bank
edit_json "$work/content/tasks/range-sum-query/task.json" 'task["status"] = "published"'
expect "published task without verification" 1

# 3. a tag that the vocabulary does not define
reset_bank
edit_json "$work/content/tasks/grid-shortest-path/task.json" 'task["topics"].append("graph-theory")'
expect "task tagged outside the vocabulary" 1

# 4. a stage pointing at a tag the vocabulary does not define
reset_bank
edit_json "$work/content/tracks.json" 'doc["tracks"][0]["stages"][0]["tags"].append("tag-that-does-not-exist")'
expect "stage names a tag outside the vocabulary" 1

# 5. the same tag twice in one stage: a band that claims one skill twice
reset_bank
edit_json "$work/content/tracks.json" 'doc["tracks"][0]["stages"][0]["tags"].append(doc["tracks"][0]["stages"][0]["tags"][0])'
expect "tag repeated inside one stage" 1

# 6. a locale the task claims but does not ship
reset_bank
rm "$work/content/tasks/range-sum-query/statement.th.md"
expect "missing statement locale" 1

# 7. an empty bank must not read as a clean scan
reset_bank
rm -rf "$work/content/tasks"
expect "empty task bank" 1

# 8. a stage that lists no tag at all
reset_bank
edit_json "$work/content/tracks.json" 'doc["tracks"][0]["stages"][0]["tags"] = []'
expect "stage lists no tag" 1

# 9. a tag no stage places: a skill nobody can be routed to
reset_bank
edit_json "$work/content/tracks.json" '[[stage["tags"].remove("simulation") for stage in track["stages"] if "simulation" in stage["tags"]] for track in doc["tracks"]]'
expect "tag placed by no stage" 1

# 12. a tag placed by one route but not the other: a skill one view silently cannot show
reset_bank
edit_json "$work/content/tracks.json" '[[stage["tags"].remove("simulation") for stage in track["stages"] if "simulation" in stage["tags"]] for track in doc["tracks"] if track["id"] == "t-usaco"]'
expect "tag missing from one route" 1

# 13. a label with no language at all: a name nobody can read
reset_bank
edit_json "$work/content/tracks.json" 'doc["tracks"][0]["stages"][0]["title"] = {}'
expect "label with no language" 1

# 10. a stage whose difficulty band is inverted
reset_bank
edit_json "$work/content/tracks.json" 'doc["tracks"][0]["stages"][1]["difficulty"] = {"min": 1600, "max": 1100}'
expect "inverted difficulty band" 1

# 11. a task no stage can ever serve
reset_bank
edit_json "$work/content/tasks/grid-shortest-path/task.json" 'task["difficulty"] = 3500'
expect "task unreachable by every stage" 1

# 14. a prerequisite that is not a tag at all: an edge drawn to nothing
reset_bank
edit_json "$work/content/prereqs.json" 'doc["edges"][0]["prereqs"].append("tag-that-does-not-exist")'
expect "prerequisite outside the vocabulary" 1

# 15. a prerequisite chain that closes on itself: a graph with no way in
reset_bank
edit_json "$work/content/prereqs.json" 'doc["edges"][[i for i, e in enumerate(doc["edges"]) if e["tag"] == "sorting"][0]]["prereqs"].append("segment-tree")'
expect "prerequisite cycle" 1

printf '\n%s case(s) behaved, %s did not\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
