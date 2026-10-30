#!/usr/bin/env bash
# Proves the upgrade gate can fail.
#
# tools/check-upgrade.sh asserts a sequence and a set of absences, so the way to prove it is worth
# running is to break the sequence in a copy and require the specific assertion to name the case. An
# exit status alone would also be produced by a harness that crashes.
#
# Usage: bash tools/falsify-upgrade-gate.sh
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

pass=0
fail=0

stage() {
  local dir="$1"
  rm -rf "$dir"
  mkdir -p "$dir/tools" "$dir/deploy"
  cp -a "$repo/tools/upgrade.sh" "$repo/tools/check-upgrade.sh" "$repo/tools/render-stack.ts" "$dir/tools/"
  cp -a "$repo/deploy/docker-compose.yml" "$dir/deploy/"
}

# case_of <name> <failing assertion it must name> <mutation run inside the staged root>
case_of() {
  local name="$1" expect="$2" mutate="$3"
  local dir="$work/$name" before after out rc
  stage "$dir"
  before="$(sha256sum "$dir/tools/upgrade.sh" | cut -d' ' -f1)"
  ( cd "$dir" && eval "$mutate" ) || { printf 'SETUP FAILED  %s\n' "$name"; fail=$((fail + 1)); return; }
  after="$(sha256sum "$dir/tools/upgrade.sh" | cut -d' ' -f1)"
  if [ "$before" = "$after" ]; then
    printf 'SETUP FAILED  %s — mutation changed nothing (stale pattern?)\n' "$name"
    fail=$((fail + 1))
    return
  fi
  out="$( cd "$dir" && bash tools/check-upgrade.sh 2>&1 )"
  rc=$?
  if [ "$rc" -eq 0 ]; then
    printf 'NOT CAUGHT    %s — the gate passed a broken upgrade\n' "$name"
    fail=$((fail + 1))
  elif [[ "$out" != *"$expect"* ]]; then
    printf 'WRONG ASSERTION %s — expected "%s", got:\n%s\n' "$name" "$expect" "$out"
    fail=$((fail + 1))
  else
    printf 'caught        %s\n' "$name"
    pass=$((pass + 1))
  fi
}

# Control: an unmodified copy must pass, or every case below is meaningless.
control="$work/control"
stage "$control"
if ( cd "$control" && bash tools/check-upgrade.sh >/dev/null 2>&1 ); then
  printf 'behaved       control (an unmodified upgrade passes)\n'
  pass=$((pass + 1))
else
  printf 'BROKEN        control — the gate rejects a correct upgrade\n'
  fail=$((fail + 1))
fi

# A release that fails its probe must be replaced by the previous one.
case_of "health-gate-dropped" "[!!]   health " \
  "sed -i 's#healthy \"\$host\" || rollback#healthy \"\$host\" || true#' tools/upgrade.sh"

# Undoing the switch is the whole point of keeping the previous compose file.
case_of "rollback-does-nothing" "[!!]   health " \
  "sed -i 's#^  docker compose -f \"\$previous_file\" up -d --wait#  :#' tools/upgrade.sh"

# A stack that half-starts must be undone, not left in place.
case_of "switch-rollback-dropped" "[!!]   switch " \
  "sed -i 's#compose up -d --wait || rollback .*#compose up -d --wait || die \"x\"#' tools/upgrade.sh"

# A migration that runs after the switch has already replaced the running code.
case_of "order-reversed" "[!!]   healthy " \
  "sed -i 's#^  pull\$#  pull_placeholder#; s#^  migrate\$#  pull#; s#^  pull_placeholder\$#  migrate#' tools/upgrade.sh"

# Without a record, the next failure has nothing to roll back to.
case_of "release-not-recorded" "[!!]   healthy " \
  "sed -i 's#^  cp \"\$compose_file\" \"\$deployed_file\"\$#  :#' tools/upgrade.sh"

# The plan is applied before the switch precisely so a bad migration is a no-op.
case_of "migration-failure-ignored" "[!!]   schema " \
  "sed -i 's#|| die \"the migration failed; nothing was switched\"#|| true#' tools/upgrade.sh"

printf '\n%s case(s) behaved, %s did not\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
