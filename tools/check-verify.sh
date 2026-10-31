#!/usr/bin/env bash
# Every task's cases must be what its own reference solution produces.
#
# The manifest is generated, like db/include/cms/db/tables.hpp: the verifier writes it, git compares it, and
# a mismatch fails the gate rather than being committed by accident. The falsification at the end points the
# verifier at a reference that produces nothing — the case must fail, because a verifier that accepts any
# output is a gate that proves nothing.
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
verifier="${1:-$repo/build/safety/judge/cms_verify_task}"
references="${2:-$repo/build/safety/references}"

if [ ! -x "$verifier" ]; then
  echo "verify gate FAILED: no verifier at $verifier; build it with: cmake --build --preset safety" >&2
  exit 1
fi

pass=0
fail=0
ok() { printf '  [ok]   %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  [!!]   %s — %s\n' "$1" "$2"; fail=$((fail + 1)); }

tasks=0
for task_path in "$repo"/content/tasks/*/; do
  task="$(basename "$task_path")"
  [ -f "$task_path/task.json" ] || continue
  slug="${task//-/_}"
  reference="$references/ref_${slug}_main"
  if [ ! -x "$reference" ]; then
    bad "$task verified against its reference" "no reference executable at $reference"
    continue
  fi
  if output="$("$verifier" "$task_path" "$reference" --write 2>&1)"; then
    ok "$task: $output"
    tasks=$((tasks + 1))
  else
    bad "$task verified against its reference" "$output"
  fi
done

[ "$tasks" -gt 0 ] && ok "$tasks task(s) were verified" || bad "at least one task was verified" "none"

if git -C "$repo" diff --quiet -- 'content/tasks/*/tests/manifest.json'; then
  ok "every committed manifest is what the reference produces"
else
  bad "every committed manifest is what the reference produces" \
      "$(git -C "$repo" diff --stat -- 'content/tasks/*/tests/manifest.json' | tail -1)"
fi

# The plan's E5.3 falsification: swap the reference for a program that prints nothing. A case whose expected
# output is not produced must be refused, not recorded.
if "$verifier" "$repo/content/tasks/range-sum-query" /bin/true >/dev/null 2>&1; then
  bad "a reference that produces nothing is refused" "the verifier accepted it"
else
  ok "a reference that produces nothing is refused"
fi

if "$verifier" "$repo/content/tasks/range-sum-query" /bin/false >/dev/null 2>&1; then
  bad "a reference that fails is refused" "the verifier accepted it"
else
  ok "a reference that fails is refused"
fi

printf '\n%s assertion(s) behaved, %s did not\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
