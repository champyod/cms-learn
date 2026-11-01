#!/usr/bin/env bash
# Proves the claim gate can fail.
#
# "A claim is exclusive and a dead worker's row comes back" is worth asserting only if the assertion dies the
# moment the statement stops doing either. Each case edits the extracted statement in a throwaway copy and
# requires the refusal that names what broke — a gate that crashed would also exit non-zero and would prove
# nothing.
#
# The gate starts a PostgreSQL cluster, so without the server binaries there is no case to run: this script
# says it cannot be exercised and exits 77.
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
export TMPDIR="$work"

for tool in initdb pg_ctl psql pgschema sha256sum; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    printf 'cannot be exercised: %s is not installed, so the gate cannot start its cluster\n' "$tool" >&2
    exit 77
  fi
done

pass=0
fail=0

stage() {
  local dir="$1"
  rm -rf "$dir"
  mkdir -p "$dir"
  cp -a "$repo/deploy" "$repo/tools" "$repo/judge" "$dir/"
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
    printf 'NOT CAUGHT    %s — the gate passed a statement that does not hold the lease\n' "$name"
    fail=$((fail + 1))
  elif [ "$rc" -eq 77 ]; then
    printf 'NOT EXERCISED %s — the staged gate reported it could not run\n' "$name"
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
if ( cd "$control" && bash tools/check-claim.sh >/dev/null 2>&1 ); then
  printf 'behaved       control (an unmodified tree passes)\n'
  pass=$((pass + 1))
else
  printf 'BROKEN        control — the gate rejects a valid tree\n'
  fail=$((fail + 1))
fi

# A claim that only considers rows nobody has ever claimed: an abandoned claim never returns, so the row of a
# worker that died stays out of the queue for good.
case_of "lease-ignored" "claimable again — expected" \
  "bash tools/check-claim.sh" \
  "sed -i 's/(claimed_at IS NULL OR claimed_at < now() - make_interval(mins => \$2::int))/claimed_at IS NULL/' judge/src/worker.cpp"

# A claim whose lease is zero: every claim is expired the instant it is written, so a second worker takes a row
# another worker is running right now.
case_of "lease-zero" "takes none — got" \
  "bash tools/check-claim.sh" \
  "sed -i 's/claimed_at < now() - make_interval(mins => \$2::int)/claimed_at < now()/' judge/src/worker.cpp"

printf '\n%s case(s) behaved, %s did not\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
