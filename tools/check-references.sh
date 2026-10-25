#!/usr/bin/env bash
# Reference gate: every task's reference solution must build clean and reproduce its own examples.
#
# This is the machine-checked half of "a reference that cannot pass its own statement's example is a
# broken task". It is deliberately strict:
#   - -Werror, so a warning that would hide an uninitialised read fails here rather than in a contest
#   - a task with a reference but no examples/*.in fails: an unproven reference is not evidence
#   - a non-zero match count is required, so an empty bank cannot pass by doing nothing
set -uo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo" || exit 1
bin="$(mktemp -d)"
trap 'rm -rf "$bin"' EXIT

pass=0
fail=0
tasks=0

normalise() { sed -e 's/[[:space:]]*$//' -e '/^[[:space:]]*$/d'; }

for task_dir in content/tasks/*/; do
  [ -d "$task_dir" ] || continue
  task="$(basename "$task_dir")"

  mapfile -t sources < <(find "$task_dir/solutions" -name '*.cpp' 2>/dev/null | sort)
  if [ "${#sources[@]}" -eq 0 ]; then
    echo "  [FAIL] ${task}: no reference solution in solutions/"
    fail=$((fail + 1))
    continue
  fi

  mapfile -t inputs < <(find "$task_dir/examples" -name '*.in' 2>/dev/null | sort)
  if [ "${#inputs[@]}" -eq 0 ]; then
    echo "  [FAIL] ${task}: no examples/*.in — the reference cannot be proven"
    fail=$((fail + 1))
    continue
  fi

  tasks=$((tasks + 1))

  for source in "${sources[@]}"; do
    exe="$bin/${task}-$(basename "$source" .cpp)"
    if ! g++ -O2 -std=c++17 -Wall -Wextra -Werror -o "$exe" "$source"; then
      echo "  [FAIL] ${task}: $(basename "$source") does not build clean"
      fail=$((fail + 1))
      continue
    fi

    for input in "${inputs[@]}"; do
      expected="${input%.in}.out"
      if [ ! -f "$expected" ]; then
        echo "  [FAIL] ${task}: $(basename "$input") has no matching .out"
        fail=$((fail + 1))
        continue
      fi
      got="$("$exe" < "$input" 2>/dev/null | normalise)"
      want="$(normalise < "$expected")"
      if [ "$got" = "$want" ]; then
        pass=$((pass + 1))
      else
        echo "  [FAIL] ${task}/$(basename "$input"): expected [${want}] got [${got}]"
        fail=$((fail + 1))
      fi
    done
  done
done

echo "references — ${tasks} task(s), ${pass} example(s) matched, ${fail} failure(s)"

if [ "$fail" -ne 0 ]; then
  exit 1
fi
# A run that checked nothing is not a pass.
if [ "$pass" -eq 0 ]; then
  echo "references — nothing to prove; refusing to report a green run"
  exit 1
fi
