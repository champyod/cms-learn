#!/usr/bin/env bash
# Proves the package-manager gate can fail.
#
# A gate that only ever passes is decoration. Each case below breaks the single-package-manager rule in
# a throwaway copy and requires the specific message, not merely a non-zero exit: a harness that
# crashes looks like it worked.
#
# Usage: bash tools/falsify-pm-gate.sh
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

pass=0
fail=0

stage() {
  local dir="$1"
  rm -rf "$dir"
  mkdir -p "$dir"
  cp "$repo/package.json" "$repo/Makefile" "$repo/bun.lock" "$dir/"
  cp -a "$repo/tools" "$dir/tools"
}

# case_of <name> <expected substring> <command> <mutation run inside the staged root>
case_of() {
  local name="$1" expect="$2" command="$3" mutate="$4"
  local dir="$work/$name" before after out rc
  stage "$dir"
  before="$(cd "$dir" && find . -type f -exec sha256sum {} + | sort | sha256sum)"
  ( cd "$dir" && eval "$mutate" ) || { printf 'SETUP FAILED  %s\n' "$name"; fail=$((fail + 1)); return; }
  after="$(cd "$dir" && find . -type f -exec sha256sum {} + | sort | sha256sum)"
  if [ "$before" = "$after" ]; then
    printf 'SETUP FAILED  %s — mutation changed nothing (stale pattern?)\n' "$name"
    fail=$((fail + 1))
    return
  fi
  out="$( cd "$dir" && eval "$command" 2>&1 )"
  rc=$?
  if [ "$rc" -eq 0 ]; then
    printf 'NOT CAUGHT    %s — the check passed a broken tree\n' "$name"
    fail=$((fail + 1))
  elif [[ "$out" != *"$expect"* ]]; then
    printf 'WRONG MESSAGE %s — expected "%s", got:\n%s\n' "$name" "$expect" "$out"
    fail=$((fail + 1))
  else
    printf 'caught        %s\n' "$name"
    pass=$((pass + 1))
  fi
}

# Control: an untouched copy must pass, or every case below is meaningless.
control="$work/control"
stage "$control"
if ( cd "$control" && bash tools/check-pm.sh >/dev/null 2>&1 ); then
  printf 'behaved       control (an unmodified tree passes)\n'
  pass=$((pass + 1))
else
  printf 'BROKEN        control — the gate rejects a valid tree\n'
  fail=$((fail + 1))
fi

case_of "lockfile-planted" "expected exactly one lockfile at the root" \
  "bash tools/check-pm.sh" \
  "printf '{}\n' > package-lock.json"

case_of "script-invokes-npm" "another package manager is invoked" \
  "bash tools/check-pm.sh" \
  "printf '\nnpm install left-pad\n' >> tools/check-db.sh"

printf '\n%s case(s) behaved, %s did not\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
