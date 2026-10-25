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

# 4. a prerequisite pointing at a module that was renamed away
reset_bank
edit_json "$work/content/modules.json" 'doc["modules"][6]["prereqs"].append("m-renamed-away")'
expect "prerequisite that does not exist" 1

# 5. a prerequisite cycle: no order a student could follow
reset_bank
edit_json "$work/content/modules.json" 'doc["modules"][0]["prereqs"] = ["m-complexity"]'
expect "prerequisite cycle" 1

# 6. a locale the task claims but does not ship
reset_bank
rm "$work/content/tasks/range-sum-query/statement.th.md"
expect "missing statement locale" 1

# 7. an empty bank must not read as a clean scan
reset_bank
rm -rf "$work/content/tasks"
expect "empty task bank" 1

# 8. a stage naming a module that does not exist
reset_bank
edit_json "$work/content/tracks.json" 'doc["tracks"][0]["stages"][0]["modules"].append("m-does-not-exist")'
expect "stage names a missing module" 1

# 9. a module no track places
reset_bank
edit_json "$work/content/tracks.json" 'doc["tracks"][0]["stages"][0]["modules"].remove("m-simulation"); doc["tracks"][1]["stages"][0]["modules"].remove("m-simulation")'
expect "module placed by no track" 1

# 10. a stage whose difficulty band is inverted
reset_bank
edit_json "$work/content/tracks.json" 'doc["tracks"][0]["stages"][1]["difficulty"] = {"min": 1600, "max": 1100}'
expect "inverted difficulty band" 1

# 11. a task no stage can ever serve
reset_bank
edit_json "$work/content/tasks/grid-shortest-path/task.json" 'task["difficulty"] = 3500'
expect "task unreachable by every stage" 1

printf '\n%s case(s) behaved, %s did not\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
