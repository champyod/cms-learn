#!/usr/bin/env bash
# C++ safety gate (ADR 0006 §5).
#
# The language matrix puts C++ on the internet-facing path, so "we will be careful" is not a control.
# Every C++ translation unit this repo ships — api, judge, and the reference solutions that prove the
# task bank — is compiled with AddressSanitizer + UndefinedBehaviorSanitizer under -Werror, and every
# program that can be run is run. A sanitizer report is a failure, not a warning.
#
# Usage: tools/check-cpp-safety.sh [root]
set -uo pipefail

root=${1:-"$(cd "$(dirname "$0")/.." && pwd)"}
[ -d "$root" ] || { echo "cpp safety — no such root: $root" >&2; exit 2; }

bin="$(mktemp -d)"
trap 'rm -rf "$bin"' EXIT

# -std=c++23 is the decision, so a toolchain that cannot compile it fails here rather than later.
# -D_GLIBCXX_ASSERTIONS turns the standard library's unchecked paths into aborts.
cxx_flags=(-std=c++23 -O1 -g -fno-omit-frame-pointer
           -fsanitize=address,undefined -fno-sanitize-recover=all
           -D_GLIBCXX_ASSERTIONS -Wall -Wextra -Werror)

export ASAN_OPTIONS="detect_leaks=1:abort_on_error=1:strict_string_checks=1"
export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"

compiled=0
ran=0
failed=0

fail() { echo "  [FAIL] $*"; failed=$((failed + 1)); }

# Sources that ship: the two service trees, plus every reference solution in the task bank.
mapfile -t sources < <(
  find "$root/api" "$root/judge" -name '*.cpp' -not -path '*/vendor/*' 2>/dev/null
  find "$root/content/tasks" -path '*/solutions/*.cpp' 2>/dev/null
) 
mapfile -t sources < <(printf '%s\n' "${sources[@]}" | grep -v '^$' | sort -u)

for source in "${sources[@]}"; do
  rel=${source#"$root"/}
  exe="$bin/$(echo "$rel" | tr '/' '_' | sed 's/\.cpp$//')"

  if ! out=$(g++ "${cxx_flags[@]}" -o "$exe" "$source" 2>&1); then
    # Name the reason: a build failure is either a warning we refuse or a real error.
    reason="error"
    grep -qE '\[-W(error|unused|sign-compare|shadow)' <<<"$out" && reason="warning as error"
    fail "$rel does not build clean ($reason)"
    sed -e 's/^/         /' <<<"$out" | head -6
    continue
  fi
  compiled=$((compiled + 1))

  # If this source belongs to a task, run it against that task's examples under the sanitizers.
  # solutions/main.cpp → task root is one level up, not two: a path that is one level too deep finds
  # no examples/ and silently turns this gate into a compile-only check.
  task_dir=$(cd "$(dirname "$source")/.." 2>/dev/null && pwd || true)
  [ -d "$task_dir/examples" ] || continue
  for input in "$task_dir"/examples/*.in; do
    [ -f "$input" ] || continue
    if ! out=$("$exe" < "$input" 2>&1 >/dev/null); then
      fail "$rel crashes or trips a sanitizer on $(basename "$input")"
      grep -E 'ERROR: (Address|Leak)Sanitizer|runtime error' <<<"$out" | head -3 | sed -e 's/^/         /'
      continue
    fi
    ran=$((ran + 1))
  done
done

echo "cpp safety — ${compiled} file(s) compiled, ${ran} run(s) under asan+ubsan, ${failed} failure(s)"

if [ "$failed" -ne 0 ]; then exit 1; fi
# A gate that compiled nothing is not green, it is unplugged.
if [ "$compiled" -eq 0 ]; then
  echo "cpp safety — nothing to compile; refusing to report a green run"
  exit 1
fi
