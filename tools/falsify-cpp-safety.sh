#!/usr/bin/env bash
# Falsification for the C++ safety gate: each planted defect must be caught WITH THE RIGHT REASON.
#
# A gate that fails for the wrong reason is as broken as one that passes a defect — the earlier deploy
# harness reported "6 caught, 1 missed" while actually catching the wrong thing (broken YAML instead of
# judge egress), so every case here asserts the message, not only the exit code.
#
# Each case is built in its own temp root, so the repo is never mutated.
set -uo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
gate="$repo/tools/check-cpp-safety.sh"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

caught=0
missed=0

# case <name> <file to write> <source> <expected substring in the gate output>
case_() {
  local name=$1 file=$2 source=$3 expect=$4
  local root="$work/$name"
  mkdir -p "$(dirname "$root/$file")"
  printf '%s\n' "$source" >"$root/$file"

  local out status
  out=$(bash "$gate" "$root" 2>&1); status=$?

  if [ "$status" -ne 0 ] && grep -qF -- "$expect" <<<"$out"; then
    printf 'caught        %-28s (%s)\n' "$name" "$expect"
    caught=$((caught + 1))
  else
    printf 'WRONG REASON  %-28s — exit %s, expected "%s"\n' "$name" "$status" "$expect"
    sed -e 's/^/              /' <<<"$out" | head -6
    missed=$((missed + 1))
  fi
}

# A gate that runs a program must have something to run against; a clean case needs an example pair.
clean_dir() {
  local name=$1
  mkdir -p "$work/$name/content/tasks/demo/examples"
  printf '3\n' >"$work/$name/content/tasks/demo/examples/01.in"
  printf '3\n' >"$work/$name/content/tasks/demo/examples/01.out"
}

clean_dir use-after-free
case_ use-after-free "content/tasks/demo/solutions/main.cpp" \
'#include <cstdio>
int main() {
  int *p = new int(42);
  delete p;
  std::printf("%d\n", *p);
  return 0;
}' "sanitizer"

clean_dir signed-overflow
case_ signed-overflow "content/tasks/demo/solutions/main.cpp" \
'#include <climits>
#include <cstdio>
int main() {
  int x = INT_MAX;
  x += 1;
  std::printf("%d\n", x);
  return 0;
}' "sanitizer"

clean_dir warning-as-error
case_ warning-as-error "api/handler.cpp" \
'int main() {
  int unused = 7;
  return 0;
}' "warning as error"

clean_dir out-of-bounds
case_ out-of-bounds "content/tasks/demo/solutions/main.cpp" \
'#include <cstdio>
#include <vector>
int main() {
  std::vector<int> v{1, 2, 3};
  std::printf("%d\n", v[7]);
  return 0;
}' "sanitizer"

# The control: a clean file must pass, otherwise the gate is failing everything and proves nothing.
clean_dir clean-control
case_clean="content/tasks/demo/solutions/main.cpp"
mkdir -p "$work/clean-control/content/tasks/demo/solutions"
printf '%s\n' '#include <cstdio>
int main() {
  int n = 0;
  if (std::scanf("%d", &n) != 1) return 1;
  std::printf("%d\n", n);
  return 0;
}' >"$work/clean-control/$case_clean"
if out=$(bash "$gate" "$work/clean-control" 2>&1); then
  printf 'behaved       %-28s (clean file passes)\n' "clean-control"
  caught=$((caught + 1))
else
  printf 'WRONG RESULT  %-28s — clean file rejected\n' "clean-control"
  sed -e 's/^/              /' <<<"$out" | head -6
  missed=$((missed + 1))
fi

# An empty tree must not be green: "nothing to compile" is unplugged, not passing.
mkdir -p "$work/empty"
if out=$(bash "$gate" "$work/empty" 2>&1); then
  printf 'WRONG RESULT  %-28s — empty tree reported green\n' "empty-tree"
  missed=$((missed + 1))
elif grep -qF "nothing to compile" <<<"$out"; then
  printf 'behaved       %-28s (refuses an empty run)\n' "empty-tree"
  caught=$((caught + 1))
else
  printf 'WRONG REASON  %-28s — expected refusal message\n' "empty-tree"
  missed=$((missed + 1))
fi

echo
echo "${caught} case(s) behaved, ${missed} did not"
[ "$missed" -eq 0 ]
