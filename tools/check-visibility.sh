#!/usr/bin/env bash
# The task-visibility gate: the audience ranges the schema enforces, and the real serving query.
#
# The encoded audience values come from cms_principal_value, the one module allowed to compute 2^48, so
# this gate never writes one by hand. The serving half rebuilds cms_serve_tests from this tree's
# bank/src/next_task.cpp and runs its live-database cases against the real plan: a task with no
# visibility row is hidden, and a task granted to everyone is served. The rebuild is what lets the
# falsification case plant a defect in the predicate and see this gate go red (about ten seconds).
#
# Skips (exit 77) when the PostgreSQL server binaries or the C++ build are not installed.
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
seeder="${1:-$repo/build/safety/bank/cms_seed_content}"
principal_value="${2:-$repo/build/safety/bank/cms_principal_value}"
build="${3:-$repo/build/safety}"

for tool in initdb pg_ctl psql g++ ar; do
  command -v "$tool" >/dev/null 2>&1 || { echo "  skipped: $tool is not installed"; exit 77; }
done
for binary in "$seeder" "$principal_value"; do
  [ -x "$binary" ] \
    || { echo "visibility gate FAILED: no executable at $binary; build it with: cmake --build --preset safety" >&2; exit 1; }
done

work="$(mktemp -d)"
port=$((56200 + (RANDOM % 300)))
database=cmslearn_visibility_gate

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

apply_plan_to() {
  local target="$1"
  psql -X -h "$work" -p "$port" -U postgres -q -c "CREATE DATABASE $target" >/dev/null 2>&1
  PGHOST="$work" PGPORT="$port" PGDATABASE="$target" PGUSER=postgres \
    bash "$repo/tools/apply-plan.sh" >"$work/apply-$target.log" 2>&1 \
    || { echo "  [!!] apply-plan failed for $target"; tail -5 "$work/apply-$target.log"; exit 1; }
}

apply_plan_to "$database"
database_url="host=$work port=$port dbname=$database user=postgres"

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

CMSLEARN_DATABASE_URL="$database_url" \
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

rebuild_serve_tests() {
  local flags_make="$build/bank/CMakeFiles/cms_bank.dir/flags.make"
  local archive="$build/bank/libcms_bank.a"
  local tests_dir="$build/bank/tests"
  local link_file="$tests_dir/CMakeFiles/cms_serve_tests.dir/link.txt"
  local source="$repo/bank/src/next_task.cpp"
  local file
  for file in "$flags_make" "$archive" "$link_file" "$source"; do
    [ -f "$file" ] || {
      echo "  [!!] cannot rebuild the serve tests: missing $file; build with: cmake --build --preset safety --target cms_serve_tests" >&2
      exit 1
    }
  done
  mkdir -p "$work/relink"
  local defines includes compiler_flags
  defines="$(sed -n 's/^CXX_DEFINES = //p' "$flags_make")"
  includes="$(sed -n 's/^CXX_INCLUDES = //p' "$flags_make")"
  compiler_flags="$(sed -n 's/^CXX_FLAGS = //p' "$flags_make")"
  eval "/usr/bin/g++ $defines $includes $compiler_flags -c \"$source\" -o \"$work/relink/next_task.cpp.o\"" \
    || { echo "  [!!] this tree's next_task.cpp does not compile" >&2; exit 1; }
  cp "$archive" "$work/relink/libcms_bank.a"
  ar r "$work/relink/libcms_bank.a" "$work/relink/next_task.cpp.o" >/dev/null 2>&1 \
    || { echo "  [!!] cannot replace next_task in the bank archive" >&2; exit 1; }
  local link_command
  link_command="$(sed -e "s|\.\./libcms_bank\.a|\"$work/relink/libcms_bank.a\"|" \
                       -e "s|-o cms_serve_tests|-o \"$work/relink/cms_serve_tests\"|" "$link_file")"
  ( cd "$tests_dir" && eval "$link_command" ) >"$work/relink/link.log" 2>&1 \
    || { echo "  [!!] cannot relink the serve tests"; tail -3 "$work/relink/link.log" >&2; exit 1; }
  [ -x "$work/relink/cms_serve_tests" ] \
    || { echo "  [!!] the rebuilt serve tests are missing" >&2; exit 1; }
  printf '%s' "$work/relink/cms_serve_tests"
}

serve_tests="$(rebuild_serve_tests)"

# Each live case gets its own database: a case that inserts tasks must not change which task another case
# sees, and every case seeds the shipped bank itself.
live_cases=0
run_live_case() {
  local name="$1"
  live_cases=$((live_cases + 1))
  local case_database="cmslearn_visibility_case_$live_cases"
  apply_plan_to "$case_database"
  local output status
  output="$(CMSLEAN_TEST_DSN="host=$work port=$port dbname=$case_database user=postgres" \
    "$serve_tests" "$name" 2>&1)"
  status=$?
  if [ "$status" -eq 0 ] && ! grep -qi 'skipped' <<<"$output"; then
    ok "$name"
  else
    bad "$name" "$(printf '%s' "$output" | tail -6)"
  fi
}

run_live_case "the serving query follows the audience composed from a principal"
run_live_case "a task with no visibility row is never served"
run_live_case "the next task is the one in band the account has not touched"

# A session uuid must bind to the row's serial id, and the composed audience must follow that binding.
principal_a="aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeee01"
principal_b="aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeee02"
unknown_principal="aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeee03"
psql_db -c "INSERT INTO principals (id, kind, handle, status)
            VALUES ('$principal_a', 'user', 'visibility_a', 'active')" >/dev/null 2>&1
psql_db -c "INSERT INTO principals (id, kind, handle, status)
            VALUES ('$principal_b', 'user', 'visibility_b', 'active')" >/dev/null 2>&1
serial_a_stored="$(psql_db -c "SELECT serial_id FROM principals WHERE id = '$principal_a'")"
serial_a="$(CMSLEARN_DATABASE_URL="$database_url" "$principal_value" serial "$principal_a")"
if [ -n "$serial_a" ] && [ "$serial_a" = "$serial_a_stored" ]; then
  ok "a session uuid resolves to its stored serial id"
else
  bad "a session uuid resolves to its stored serial id" "lookup '$serial_a', row '$serial_a_stored'"
fi

learner_a="$("$principal_value" encode learner "$serial_a")"
audience_a="$(CMSLEARN_DATABASE_URL="$database_url" "$principal_value" audience "$principal_a" | tr ' ' ',')"
if [ "$audience_a" = "$everyone,$learner_a" ]; then
  ok "the request audience is everyone plus the encoded viewer"
else
  bad "the request audience is everyone plus the encoded viewer" "audience '$audience_a'"
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
