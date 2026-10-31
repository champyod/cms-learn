#!/usr/bin/env bash
# Imports the shipped bank into a scratch cluster and proves the import is idempotent.
#
# The plan's falsification is the third assertion onwards: a second seed of the same bank must write
# nothing, leave the row counts identical, and not move the content version. A seeder that rewrote every
# row on every start would still "work" — it would just make every restart a migration.
#
# Skips (exit 77) when the PostgreSQL server binaries are not installed, so a checkout without them
# reports a skip rather than a failure.
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
seeder="${1:-$repo/build/safety/bank/cms_seed_content}"

if [ ! -x "$seeder" ]; then
  echo "content gate FAILED: no seeder at $seeder; build it with: cmake --build --preset safety" >&2
  exit 1
fi

for tool in initdb pg_ctl psql; do
  command -v "$tool" >/dev/null 2>&1 || { echo "  skipped: $tool is not installed"; exit 77; }
done

if [ ! -f "$repo/deploy/roles.sql" ] || [ ! -f "$repo/tools/apply-plan.sh" ]; then
  echo "  skipped: this tree has no deploy/ or tools/"
  exit 77
fi

work="$(mktemp -d)"
port=$((56000 + (RANDOM % 500)))
database=cmslearn_content_gate

cleanup() {
  pg_ctl -D "$work/data" -m immediate stop >/dev/null 2>&1
  rm -rf "$work"
}
trap cleanup EXIT

pass=0
fail=0
ok() { printf '  [ok]   %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  [!!]   %s — %s\n' "$1" "$2"; fail=$((fail + 1)); }

psql_db() { psql -X -h "$work" -p "$port" -U postgres -d "$database" -tAc "$1" 2>/dev/null; }

initdb -D "$work/data" -U postgres -A trust >"$work/initdb.log" 2>&1 || { echo "  [!!] initdb failed"; exit 1; }
pg_ctl -D "$work/data" -o "-c listen_addresses='' -c unix_socket_directories=$work -p $port" \
  -l "$work/pg.log" start >/dev/null 2>&1 || { echo "  [!!] pg_ctl failed"; exit 1; }
sleep 1
psql -X -h "$work" -p "$port" -U postgres -q -v ON_ERROR_STOP=1 -f "$repo/deploy/roles.sql" >/dev/null 2>&1 \
  || { echo "  [!!] deploy/roles.sql failed"; exit 1; }
psql -X -h "$work" -p "$port" -U postgres -q -c "CREATE DATABASE $database" >/dev/null 2>&1
PGHOST="$work" PGPORT="$port" PGDATABASE="$database" PGUSER=postgres \
  bash "$repo/tools/apply-plan.sh" >"$work/apply.log" 2>&1 \
  || { echo "  [!!] apply-plan failed"; tail -5 "$work/apply.log"; exit 1; }

export CMSLEARN_DATABASE_URL="host=$work port=$port dbname=$database user=postgres"

expected_tasks=$(find "$repo/content/tasks" -mindepth 2 -maxdepth 2 -name task.json | wc -l)
expected_topics=$(python3 -c "import json,sys;print(len(json.load(open(sys.argv[1]))['topics']))" \
  "$repo/content/topics.json" 2>/dev/null)
expected_examples=$(find "$repo/content/tasks" -name '*.in' | wc -l)

first="$("$seeder" --content "$repo/content" 2>&1)"
if [ $? -eq 0 ]; then ok "the first import succeeds"; else bad "the first import succeeds" "$first"; fi
if [[ "$first" == *"(changed)"* ]]; then ok "the first import reports a change"; else bad "the first import reports a change" "$first"; fi

version_after_first="$(psql_db 'SELECT version FROM content_seed')"
tasks="$(psql_db 'SELECT count(*) FROM tasks')"
topics="$(psql_db 'SELECT count(*) FROM topics')"
examples="$(psql_db 'SELECT count(*) FROM task_examples')"

[ "$tasks" = "$expected_tasks" ] && ok "every task directory became $tasks row(s)" \
  || bad "every task directory became a row" "expected $expected_tasks, found $tasks"
[ "$topics" = "$expected_topics" ] && ok "the whole vocabulary landed ($topics topic(s))" \
  || bad "the whole vocabulary landed" "expected $expected_topics, found $topics"
[ "$examples" = "$expected_examples" ] && ok "every example landed ($examples row(s))" \
  || bad "every example landed" "expected $expected_examples, found $examples"

duplicates="$(psql_db 'SELECT count(*) FROM (SELECT id, version FROM tasks GROUP BY id, version HAVING count(*) > 1) AS d')"
[ "$duplicates" = "0" ] && ok "no task was inserted twice" || bad "no task was inserted twice" "$duplicates duplicate key(s)"

hashes="$(psql_db "SELECT count(*) FROM tasks WHERE content_hash !~ '^[0-9a-f]{64}\$'")"
[ "$hashes" = "0" ] && ok "every task carries a sha-256 digest" || bad "every task carries a digest" "$hashes bad"

second="$("$seeder" --content "$repo/content" 2>&1)"
if [[ "$second" == *"(unchanged)"* ]]; then ok "the second import of the same bank changes nothing"; \
  else bad "the second import changes nothing" "$second"; fi

version_after_second="$(psql_db 'SELECT version FROM content_seed')"
[ "$version_after_first" = "$version_after_second" ] \
  && ok "the content version did not move ($version_after_first)" \
  || bad "the content version did not move" "$version_after_first -> $version_after_second"

[ "$(psql_db 'SELECT count(*) FROM tasks')" = "$tasks" ] \
  && ok "the second import left the task count alone" || bad "the second import left the task count alone" "it moved"

third="$("$seeder" --content "$repo/content" 2>&1)"
[ "$(psql_db 'SELECT version FROM content_seed')" = "$version_after_first" ] \
  && ok "a third import still reports version $version_after_first" || bad "a third import is still a no-op" "$third"

printf '\n%s assertion(s) behaved, %s did not\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
