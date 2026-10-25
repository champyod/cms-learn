#!/usr/bin/env bash
# Falsification for the reference gate, same rule as the content gate: a green run proves nothing
# until the gate is seen going red. Works on a throwaway copy; the tree is never mutated.
set -uo pipefail

src="$(cd "$(dirname "$0")/.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

reset() { rm -rf "$work/content" "$work/tools"; cp -a "$src/content" "$src/tools" "$work/"; }
run_gate() { ( cd "$work" && bash tools/check-references.sh >/dev/null 2>&1 ); }

pass=0
fail=0
expect() {
  local name="$1" expected="$2" got
  if run_gate; then got=0; else got=1; fi
  if [ "$got" = "$expected" ]; then
    printf '  [ok]   %-44s gate exit %s (expected %s)\n' "$name" "$got" "$expected"
    pass=$((pass + 1))
  else
    printf '  [FAIL] %-44s gate exit %s (expected %s)\n' "$name" "$got" "$expected"
    fail=$((fail + 1))
  fi
}

echo "reference gate falsification"

reset
expect "honest references" 0

reset
printf '8\n12\n' > "$work/content/tasks/range-sum-query/examples/1.out"
expect "expected output that the reference disagrees with" 1

reset
rm -rf "$work/content/tasks/grid-shortest-path/examples"
expect "task with no examples to prove against" 1

reset
printf 'int main() { this is not c++ }\n' > "$work/content/tasks/grid-shortest-path/solutions/main.cpp"
expect "reference that does not compile" 1

reset
rm -rf "$work/content/tasks"
expect "no tasks at all" 1

printf '\n%s case(s) behaved, %s did not\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
