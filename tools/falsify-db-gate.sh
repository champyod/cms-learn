#!/usr/bin/env bash
# Proves the database-layer gate can fail.
#
# The gate's claim is that db/include/cms/db/tables.hpp is exactly what the applied plan renders: the
# same tables, the same columns, byte for byte. Each case below breaks that in a throwaway copy and
# requires the message that names the disagreement — a header that merely fails to compare, or a
# harness that crashes, would also exit non-zero and would prove nothing.
#
# The gate starts a PostgreSQL cluster, so without the server binaries there is no catch to count and
# no pass either: this script says the gate cannot be exercised and exits 77.
#
# Usage: bash tools/falsify-db-gate.sh
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
export TMPDIR="$work"

for tool in initdb pg_ctl createdb psql; do
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
  cp -a "$repo/db" "$repo/deploy" "$repo/tools" "$dir/"
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
    printf 'NOT CAUGHT    %s — the gate passed a header the schema does not render\n' "$name"
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
if ( cd "$control" && bash tools/check-db.sh >/dev/null 2>&1 ); then
  printf 'behaved       control (an unmodified tree passes)\n'
  pass=$((pass + 1))
else
  printf 'BROKEN        control — the gate rejects a valid tree\n'
  fail=$((fail + 1))
fi

# A hand edit: one column tag rewritten, with the table and column named in the report.
case_of "column-tag-hand-edited" "committed header: Progress.SolvedAt" \
  "bash tools/check-db.sh" \
  "sed -i 's/(solved_at, solvedAt);/(solved_at, solvedAtGate);/' db/include/cms/db/tables.hpp"

# A column the schema gained: the header never learned about it.
case_of "column-struct-missing" "the committed header first differs at line" \
  "bash tools/check-db.sh" \
  "sed -i '/^  struct SolvedAt {$/,/^  };$/d' db/include/cms/db/tables.hpp"

# A table the schema lost: the header still declares a struct for it.
case_of "table-struct-invented" "in the header, no table in the schema: gate_probe" \
  "bash tools/check-db.sh" \
  "sed -i 's|^}  // namespace cms::db\$|struct GateProbe_ {\n  SQLPP_CREATE_NAME_TAG_FOR_SQL_AND_CPP(gate_probe, gateProbe);\n};\n\n}  // namespace cms::db|' db/include/cms/db/tables.hpp"

# A table the header lost: the schema has it and no struct names it.
case_of "table-struct-removed" "in the schema, no struct in the header: progress" \
  "bash tools/check-db.sh" \
  "sed -i '/^struct Progress_ {\$/,/^};\$/d' db/include/cms/db/tables.hpp"

case_of "header-deleted" "db/include/cms/db/tables.hpp is missing" \
  "bash tools/check-db.sh" \
  "rm db/include/cms/db/tables.hpp"

printf '\n%s case(s) behaved, %s did not\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
