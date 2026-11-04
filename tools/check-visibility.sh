#!/usr/bin/env bash
# The task-visibility gate: the audience ranges the schema enforces, and one serving query.
#
# The encoded audience values come from cms_principal_value, the one module allowed to compute 2^48, so
# this gate never writes one by hand. The serving half mirrors bank::next_unseen_task against the real
# plan: a task with no visibility row is hidden, and a task with the everyone row is served.
#
# Skips (exit 77) when the PostgreSQL server binaries are not installed.
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
seeder="${1:-$repo/build/safety/bank/cms_seed_content}"
principal_value="${2:-$repo/build/safety/bank/cms_principal_value}"

for tool in initdb pg_ctl psql; do
  command -v "$tool" >/dev/null 2>&1 || { echo "  skipped: $tool is not installed"; exit 77; }
done
for binary in "$seeder" "$principal_value"; do
  [ -x "$binary" ] \
    || { echo "visibility gate FAILED: no executable at $binary; build it with: cmake --build --preset safety" >&2; exit 1; }
done

work="$(mktemp -d)"
port=$((56200 + (RANDOM % 300)))
database=cmslearn_visibility_gate
account="11111111-2222-4333-8444-555555555555"

cleanup() {
  pg_ctl -D "$work/data" -m immediate stop >/dev/null 2>&1
  rm -rf "$work"
}
trap cleanup EXIT

pass=0
fail=0
ok() { printf '  [ok]   %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  [!!]   %s — %s\n' "$1" "$2"; fail=$((fail + 1)); }

psql_db() { psql -X -h "$work" -p "$port" -U postgres -d "$database" -tAq "$@"; }

initdb -D "$work/data" -U postgres -A trust >"$work/initdb.log" 2>&1 \
  || { echo "  [!!] initdb failed"; exit 1; }
pg_ctl -D "$work/data" -o "-c listen_addresses='' -c unix_socket_directories=$work -p $port" \
  -l "$work/pg.log" start >/dev/null 2>&1 || { echo "  [!!] pg_ctl failed"; exit 1; }
sleep 1
psql -X -h "$work" -p "$port" -U postgres -q -v ON_ERROR_STOP=1 -f "$repo/deploy/roles.sql" >/dev/null 2>&1 \
  || { echo "  [!!] deploy/roles.sql failed"; exit 1; }
psql -X -h "$work" -p "$port" -U postgres -q -c "CREATE DATABASE $database" >/dev/null 2>&1
PGHOST="$work" PGPORT="$port" PGDATABASE="$database" PGUSER=postgres \
  bash "$repo/tools/apply-plan.sh" >"$work/apply.log" 2>&1 \
  || { echo "  [!!] apply-plan failed"; tail -5 "$work/apply.log"; exit 1; }

everyone="$("$principal_value" everyone)"
limit="$("$principal_value" limit)"
user7="$("$principal_value" encode learner 7)"
admin8="$("$principal_value" encode administrator 8)"
team9="$("$principal_value" encode team 9)"
max_team="$("$principal_value" encode team "$limit")"
above_max="$((max_team + 1))"

decoded="$("$principal_value" decode "$user7")"
if [ "$decoded" = "learner 7" ]; then
  ok "the module encodes and decodes a learner principal"
else
  bad "the module encodes and decodes a learner principal" "decode said '$decoded'"
fi
if "$principal_value" decode 1 >/dev/null 2>&1; then
  bad "the module refuses a value outside the ranges" "decode 1 succeeded"
else
  ok "the module refuses a value outside the ranges"
fi

if [ "$("$principal_value" default learner)" = "$everyone" ]; then
  ok "a learner-created task carries the everyone row"
else
  bad "a learner-created task carries the everyone row" "$("$principal_value" default learner)"
fi
if [ -z "$("$principal_value" default administrator)" ]; then
  ok "an administrator-created task carries no rows"
else
  bad "an administrator-created task carries no rows" "$("$principal_value" default administrator)"
fi

CMSLEARN_DATABASE_URL="host=$work port=$port dbname=$database user=postgres" \
  "$seeder" --content "$repo/content" >"$work/seed.log" 2>&1 \
  || { echo "  [!!] the seeder failed"; tail -5 "$work/seed.log"; exit 1; }
imported="$(psql_db -c "SELECT count(*) FROM task_visibility")"
if [ "$imported" = "0" ]; then
  ok "imported tasks carry no visibility rows"
else
  bad "imported tasks carry no visibility rows" "$imported row(s) after the import"
fi

insert_task() {
  psql_db -c "INSERT INTO tasks (id, version, status, title_en, title_th, difficulty,
                                 time_limit_ms, memory_limit_mb, checker, origin, verification,
                                 solution, content_hash)
              VALUES ('$1', 1, 'published', '$1', '$1', 800, 1000, 256, 'token', 'original',
                      'verified', 'int main() {}', repeat('a', 64))" >/dev/null 2>&1
  psql_db -c "INSERT INTO task_topics (task_id, task_version, topic) VALUES ('$1', 1, 'io')" >/dev/null 2>&1
}

insert_task check-task
for value in "$everyone" "$user7" "$admin8" "$team9"; do
  psql_db -c "INSERT INTO task_visibility (task_id, principal_id) VALUES ('check-task', $value)" >/dev/null 2>&1 \
    || bad "the CHECK accepts a legal range value" "$value was refused"
done
accepted="$(psql_db -c "SELECT count(*) FROM task_visibility WHERE task_id = 'check-task'")"
if [ "$accepted" = "4" ]; then
  ok "the CHECK accepts every legal range value"
else
  bad "the CHECK accepts every legal range value" "$accepted of 4 rows landed"
fi
if psql_db -c "INSERT INTO task_visibility (task_id, principal_id) VALUES ('check-task', $everyone)" >/dev/null 2>&1; then
  bad "the primary key refuses a duplicate grant" "the second everyone row was accepted"
else
  ok "the primary key refuses a duplicate grant"
fi
for value in "-1" "1" "$above_max"; do
  if psql_db -c "INSERT INTO task_visibility (task_id, principal_id) VALUES ('check-task', $value)" >/dev/null 2>&1; then
    bad "the CHECK refuses $value" "the value was accepted"
  else
    ok "the CHECK refuses $value"
  fi
done

insert_task hidden-task
insert_task public-task
insert_task other-user-task
insert_task team-task
insert_task learner-task
psql_db -c "INSERT INTO task_visibility (task_id, principal_id) VALUES ('public-task', $everyone)" >/dev/null 2>&1
psql_db -c "INSERT INTO task_visibility (task_id, principal_id) VALUES ('other-user-task', $user7 + 1)" >/dev/null 2>&1
psql_db -c "INSERT INTO task_visibility (task_id, principal_id) VALUES ('team-task', $team9)" >/dev/null 2>&1
psql_db -c "INSERT INTO task_visibility (task_id, principal_id) VALUES ('learner-task', $("$principal_value" default learner))" >/dev/null 2>&1

visibility_filter="EXISTS (SELECT 1 FROM task_visibility AS v WHERE v.task_id = t.id AND v.principal_id = ANY(ARRAY[$everyone, $user7, $team9]::bigint[]))"
read -r -d '' query <<SQL
SELECT t.id
FROM tasks AS t
WHERE t.status = 'published'
  AND t.difficulty >= 800 AND t.difficulty <= 1200
  AND EXISTS (SELECT 1 FROM task_topics AS tt
              WHERE tt.task_id = t.id AND tt.task_version = t.version AND tt.topic IN ('io'))
  AND NOT EXISTS (SELECT 1 FROM progress AS p
                  WHERE p.account_id = '$account' AND p.task_id = t.id AND p.task_version = t.version)
  AND $visibility_filter
ORDER BY t.difficulty, t.id
SQL
served="$(psql_db -c "$query")"

if grep -qx 'public-task' <<<"$served"; then
  ok "a task with the everyone row is served to a learner"
else
  bad "a task with the everyone row is served to a learner" "served: ${served:-none}"
fi
if grep -qx 'team-task' <<<"$served"; then
  ok "a task granted to the learner's team is served"
else
  bad "a task granted to the learner's team is served" "served: ${served:-none}"
fi
if grep -qx 'learner-task' <<<"$served"; then
  ok "a learner-created task is served to everyone"
else
  bad "a learner-created task is served to everyone" "served: ${served:-none}"
fi
if grep -qx 'hidden-task' <<<"$served"; then
  bad "a task with no visibility row is not served" "served: $served"
else
  ok "a task with no visibility row is not served"
fi
if grep -qx 'other-user-task' <<<"$served"; then
  bad "a task granted to another learner is not served" "served: $served"
else
  ok "a task granted to another learner is not served"
fi

# A session uuid must bind to the row's serial id, and the composed audience must follow that binding.
principal_a="aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeee01"
principal_b="aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeee02"
unknown_principal="aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeee03"
database_url="host=$work port=$port dbname=$database user=postgres"
psql_db -c "INSERT INTO principals (id, kind, handle, status)
            VALUES ('$principal_a', 'user', 'visibility_a', 'active')" >/dev/null 2>&1
psql_db -c "INSERT INTO principals (id, kind, handle, status)
            VALUES ('$principal_b', 'user', 'visibility_b', 'active')" >/dev/null 2>&1
serial_a_stored="$(psql_db -c "SELECT serial_id FROM principals WHERE id = '$principal_a'")"
serial_b_stored="$(psql_db -c "SELECT serial_id FROM principals WHERE id = '$principal_b'")"
serial_a="$(CMSLEARN_DATABASE_URL="$database_url" "$principal_value" serial "$principal_a")"
if [ -n "$serial_a" ] && [ "$serial_a" = "$serial_a_stored" ]; then
  ok "a session uuid resolves to its stored serial id"
else
  bad "a session uuid resolves to its stored serial id" "lookup '$serial_a', row '$serial_a_stored'"
fi

learner_a="$("$principal_value" encode learner "$serial_a")"
audience_a="$(CMSLEARN_DATABASE_URL="$database_url" "$principal_value" audience "$principal_a" | tr ' ' ',')"
audience_b="$(CMSLEARN_DATABASE_URL="$database_url" "$principal_value" audience "$principal_b" | tr ' ' ',')"
if [ "$audience_a" = "$everyone,$learner_a" ]; then
  ok "the request audience is everyone plus the encoded viewer"
else
  bad "the request audience is everyone plus the encoded viewer" "audience '$audience_a'"
fi

insert_task bound-task
psql_db -c "INSERT INTO task_visibility (task_id, principal_id) VALUES ('bound-task', $learner_a)" >/dev/null 2>&1
binding_query() {
  psql_db -c "SELECT t.id
              FROM tasks AS t
              WHERE t.status = 'published'
                AND t.difficulty >= 800 AND t.difficulty <= 1200
                AND EXISTS (SELECT 1 FROM task_topics AS tt
                            WHERE tt.task_id = t.id AND tt.task_version = t.version
                              AND tt.topic IN ('io'))
                AND NOT EXISTS (SELECT 1 FROM progress AS p
                                WHERE p.account_id = '$account'
                                  AND p.task_id = t.id AND p.task_version = t.version)
                AND EXISTS (SELECT 1 FROM task_visibility AS v
                            WHERE v.task_id = t.id
                              AND v.principal_id = ANY(ARRAY[$1]::bigint[]))"
}
served_a="$(binding_query "$audience_a")"
served_b="$(binding_query "$audience_b")"
if grep -qx 'bound-task' <<<"$served_a"; then
  ok "a learner's serial grant serves that learner"
else
  bad "a learner's serial grant serves that learner" "served: ${served_a:-none}"
fi
if grep -qx 'bound-task' <<<"$served_b"; then
  bad "a learner's serial grant does not serve another learner" "served: $served_b"
else
  ok "a learner's serial grant does not serve another learner"
fi

if CMSLEARN_DATABASE_URL="$database_url" "$principal_value" serial "$unknown_principal" >/dev/null 2>&1; then
  bad "the serial lookup refuses a uuid with no principal row" "a serial id was returned"
else
  ok "the serial lookup refuses a uuid with no principal row"
fi
if CMSLEARN_DATABASE_URL="$database_url" "$principal_value" audience "$unknown_principal" >/dev/null 2>&1; then
  bad "an unknown principal is refused, not widened to everyone" "an audience was composed"
else
  ok "an unknown principal is refused, not widened to everyone"
fi

printf '\n%s assertion(s) behaved, %s did not\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
