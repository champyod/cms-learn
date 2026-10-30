#!/usr/bin/env bash
# Database layer gate.
#
# db/include/cms/db/tables.hpp is generated, and every C++ query in the database layer depends on it
# naming every column the plan creates. This gate starts the same scratch cluster tools/check-schema.sh
# starts, applies deploy/plan through the production wrapper, renders the header with
# tools/render-tables.sh and compares the two byte for byte. A hand edit, a column the schema gained,
# and a table the schema lost all fail here, and the failure names the table and column that differ.
#
# Needs PostgreSQL server binaries (initdb, pg_ctl) matching the version in release.toml.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
committed="$repo/db/include/cms/db/tables.hpp"
work="$(mktemp -d)"
port=55433
database=cmslearn_gate

trap 'pg_ctl -D "$work/data" -m immediate stop >/dev/null 2>&1 || true; rm -rf "$work"' EXIT

die() { printf 'db gate FAILED: %s\n' "$*" >&2; exit 1; }
note() { printf '  %s\n' "$*"; }

for tool in initdb pg_ctl createdb psql; do
  command -v "$tool" >/dev/null 2>&1 || die "$tool is not installed"
done

[ -f "$committed" ] || die "db/include/cms/db/tables.hpp is missing; render it from the plan"

initdb -D "$work/data" -U postgres -A trust >"$work/initdb.log" 2>&1 \
  || die "initdb failed; see $work/initdb.log"
pg_ctl -D "$work/data" \
  -o "-c listen_addresses='' -c unix_socket_directories=$work -p $port" \
  -l "$work/server.log" -w start >/dev/null 2>&1 \
  || die "the scratch cluster did not start; see $work/server.log"

psql -X -h "$work" -p "$port" -U postgres -q -v ON_ERROR_STOP=1 -f "$repo/deploy/roles.sql" >/dev/null \
  || die "deploy/roles.sql failed"
createdb -h "$work" -p "$port" -U postgres "$database" || die "could not create $database"

PGHOST="$work" PGPORT="$port" PGDATABASE="$database" PGUSER=postgres \
  bash "$repo/tools/apply-plan.sh" >"$work/apply.log" 2>&1 \
  || die "tools/apply-plan.sh failed; see $work/apply.log"

PGHOST="$work" PGPORT="$port" PGDATABASE="$database" PGUSER=postgres \
  bash "$repo/tools/render-tables.sh" "$work/tables.hpp" \
  || die "tools/render-tables.sh could not describe the schema"

# The header must describe exactly the tables that exist before any byte is compared, so a missing or
# invented table is reported as such rather than as a diff of unrelated lines.
psql -X -h "$work" -p "$port" -U postgres -d "$database" -q -t -A -v ON_ERROR_STOP=1 -c \
  "select table_name from information_schema.tables
   where table_schema = 'public' and table_type = 'BASE TABLE'" \
  | LC_ALL=C sort > "$work/db-tables.txt"

grep -o '^  SQLPP_CREATE_NAME_TAG_FOR_SQL_AND_CPP([a-z0-9_]*,' "$committed" \
  | sed 's/.*(\([a-z0-9_]*\),/\1/' | LC_ALL=C sort > "$work/header-tables.txt" \
  || die "the committed header declares no tables"

missing_in_header="$(comm -23 "$work/db-tables.txt" "$work/header-tables.txt")"
invented_in_header="$(comm -13 "$work/db-tables.txt" "$work/header-tables.txt")"
if [ -n "$missing_in_header" ] || [ -n "$invented_in_header" ]; then
  printf 'db gate FAILED: the header and the schema disagree about which tables exist\n' >&2
  if [ -n "$missing_in_header" ]; then
    printf '%s\n' "$missing_in_header" | sed 's/^/  in the schema, no struct in the header: /' >&2
  fi
  if [ -n "$invented_in_header" ]; then
    printf '%s\n' "$invented_in_header" | sed 's/^/  in the header, no table in the schema: /' >&2
  fi
  exit 1
fi

first_differing_line() {
  awk 'NR == FNR { generated[FNR] = $0; next }
       $0 != generated[FNR] { print FNR; exit }' "$1" "$2"
}

nearest_table() {
  head -n "$2" "$1" \
    | awk '/^struct [A-Za-z0-9_]+_ \{$/ { name = $2; sub(/_$/, "", name) } END { print name }'
}

nearest_column() {
  head -n "$2" "$1" | awk '/^  struct [A-Za-z0-9]+ \{$/ { name = $2 } END { print name }'
}

report_drift() {
  local generated="$1" committed_file="$2" line from to
  line="$(first_differing_line "$generated" "$committed_file")"
  [ -n "$line" ] || line="$(( $(wc -l < "$committed_file") + 1 ))"
  from=$(( line > 3 ? line - 3 : 1 ))
  to=$(( line + 3 ))
  printf 'db gate FAILED: the committed header first differs at line %s\n' "$line" >&2
  printf '  committed header: %s.%s\n' \
    "$(nearest_table "$committed_file" "$line")" "$(nearest_column "$committed_file" "$line")" >&2
  printf '  the schema:       %s.%s\n' \
    "$(nearest_table "$generated" "$line")" "$(nearest_column "$generated" "$line")" >&2
  printf '  --- committed\n' >&2
  sed -n "${from},${to}p" "$committed_file" | sed 's/^/  /' >&2
  printf '  --- generated from the schema\n' >&2
  sed -n "${from},${to}p" "$generated" | sed 's/^/  /' >&2
  exit 1
}

cmp -s "$work/tables.hpp" "$committed" \
  || report_drift "$work/tables.hpp" "$committed"

note "$(wc -l < "$work/db-tables.txt") table(s) in public"
note "$(grep -c '^  struct ' "$committed") column(s) described"
note "db/include/cms/db/tables.hpp is what the plan renders"
