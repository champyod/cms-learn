#!/usr/bin/env bash
# The judge gate: a claimed submission is compiled, run, checked and written back as one verdict.
#
# The sandbox is `judge/tests/stubs/local-isolate.sh`. This host has no isolate, so the gate proves the run
# path and the verdict write, not containment — the container case plus `check:cpp` covers containment.
#
# Skips (exit 77) when the PostgreSQL server binaries are not installed.
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
runner="${1:-$repo/build/safety/judge/cms_judge_run}"
checker="${2:-$repo/build/safety/judge/cms_checker_token}"
seeder="${3:-$repo/build/safety/bank/cms_seed_content}"
compiler="${4:-g++}"

for tool in initdb pg_ctl psql; do
  command -v "$tool" >/dev/null 2>&1 || { echo "  skipped: $tool is not installed"; exit 77; }
done
for binary in "$runner" "$checker" "$seeder"; do
  [ -x "$binary" ] \
    || { echo "judge gate FAILED: no executable at $binary; build it with: cmake --build --preset safety" >&2; exit 1; }
done
command -v "$compiler" >/dev/null 2>&1 \
  || { echo "judge gate FAILED: the compiler $compiler is not installed" >&2; exit 1; }

accepted_source="$repo/content/tasks/range-sum-query/solutions/main.cpp"
wrong_source="$repo/judge/tests/fixtures/wrong-answer.cpp"
isolate_source="$repo/judge/tests/stubs/local-isolate.sh"
for file in "$accepted_source" "$wrong_source" "$isolate_source"; do
  [ -f "$file" ] || { echo "judge gate FAILED: missing $file" >&2; exit 1; }
done

work="$(mktemp -d)"
port=$((56300 + (RANDOM % 300)))
database=cmslearn_judge_gate
account="11111111-2222-4333-8444-555555555555"
task=range-sum-query

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
CMSLEARN_DATABASE_URL="host=$work port=$port dbname=$database user=postgres" \
  "$seeder" --content "$repo/content" >"$work/seed.log" 2>&1 \
  || { echo "  [!!] the seeder failed"; tail -5 "$work/seed.log"; exit 1; }

cp "$isolate_source" "$work/isolate"
chmod +x "$work/isolate"

psql_db -c "INSERT INTO principals (id, kind, handle, status)
            VALUES ('$account', 'user', 'judge_probe', 'active')" >/dev/null 2>&1 \
  || { echo "  [!!] could not insert the probe account"; exit 1; }

sql_text() { sed "s/'/''/g" "$1"; }
insert_submission() {
  psql_db -c "INSERT INTO submissions (account_id, task_id, task_version, source)
              VALUES ('$account', '$task', 1, '$(sql_text "$1")') RETURNING id"
}
accepted_id="$(insert_submission "$accepted_source")"
wrong_id="$(insert_submission "$wrong_source")"
if [ -z "$accepted_id" ] || [ -z "$wrong_id" ]; then
  echo "  [!!] could not insert the two probe submissions"
  exit 1
fi

database_url="host=$work port=$port dbname=$database user=postgres"
judged=0
for _ in 1 2; do
  output="$(CMSLEARN_DATABASE_URL="$database_url" "$runner" --isolate "$work/isolate" \
    --checker "$checker" --compiler "$compiler" --worker judge-gate 2>&1)"
  status=$?
  if [ "$status" -eq 0 ] && printf '%s' "$output" | grep -q 'verdict '; then
    judged=$((judged + 1))
  else
    bad "the runner judged a claimed submission" "exit $status: $output"
  fi
done
if [ "$judged" -eq 2 ]; then
  ok "the runner judged both claimed submissions"
fi

verdict_of() { psql_db -c "SELECT verdict FROM submissions WHERE id = '$1'"; }
detail_of() { psql_db -c "SELECT coalesce(verdict_detail, '') FROM submissions WHERE id = '$1'"; }
claimed_of() { psql_db -c "SELECT coalesce(claimed_by, '') FROM submissions WHERE id = '$1'"; }
judged_at_of() {
  psql_db -c "SELECT CASE WHEN judged_at IS NULL THEN '' ELSE 'judged' END FROM submissions WHERE id = '$1'"
}

accepted_verdict="$(verdict_of "$accepted_id")"
wrong_verdict="$(verdict_of "$wrong_id")"

if [ "$accepted_verdict" = "ok" ]; then
  ok "the correct submission reaches accepted (submissions.verdict = ok)"
else
  bad "the correct submission reaches accepted (submissions.verdict = ok)" "verdict: ${accepted_verdict:-none}"
fi
if [ "$wrong_verdict" = "wrong_answer" ]; then
  ok "the wrong-answer submission is wrong_answer, never accepted"
else
  bad "the wrong-answer submission is wrong_answer, never accepted" "verdict: ${wrong_verdict:-none}"
fi
if [ "$(judged_at_of "$accepted_id")" = "judged" ] && [ -z "$(claimed_of "$accepted_id")" ]; then
  ok "the accepted row carries its moment and no claim"
else
  bad "the accepted row carries its moment and no claim" "judged_at or claim left behind"
fi
if [ "$(judged_at_of "$wrong_id")" = "judged" ] && [ -z "$(claimed_of "$wrong_id")" ]; then
  ok "the wrong-answer row carries its moment and no claim"
else
  bad "the wrong-answer row carries its moment and no claim" "judged_at or claim left behind"
fi
if [ -n "$(detail_of "$wrong_id")" ]; then
  ok "the wrong-answer row names its reason"
else
  bad "the wrong-answer row names its reason" "verdict_detail is empty"
fi

printf '\n%s assertion(s) behaved, %s did not\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
