#!/usr/bin/env bash
# The unseen-task query must be served by progress's index, and the gate must be able to prove the
# index is what does it.
#
# The falsification is the second half: drop `progress_account_task_idx` and the same query must fall
# back to a sequential scan. That only means something if the tables are large enough for the planner to
# have a choice, so this inserts twenty thousand tasks and twenty thousand attempts before explaining.
#
# Skips (exit 77) when the PostgreSQL server binaries are not installed.
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
seeder="${1:-$repo/build/safety/bank/cms_seed_content}"

for tool in initdb pg_ctl psql; do
  command -v "$tool" >/dev/null 2>&1 || { echo "  skipped: $tool is not installed"; exit 77; }
done
if [ ! -x "$seeder" ]; then
  echo "query-plan gate FAILED: no seeder at $seeder; build it with: cmake --build --preset safety" >&2
  exit 1
fi

work="$(mktemp -d)"
port=$((56500 + (RANDOM % 400)))
database=cmslearn_plan_gate
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

CMSLEARN_DATABASE_URL="host=$work port=$port dbname=$database user=postgres" \
  "$seeder" --content "$repo/content" >/dev/null 2>&1 \
  || { echo "  [!!] the seeder failed"; exit 1; }

# A learner and enough volume that the planner has a real choice.
psql_db <<SQL >/dev/null 2>&1
INSERT INTO principals (id, kind, handle, status)
VALUES ('$account', 'user', 'plan_probe', 'active');

INSERT INTO tasks (id, version, status, title_en, title_th, difficulty, time_limit_ms,
                   memory_limit_mb, checker, origin, verification, solution, content_hash)
SELECT 'synthetic-' || value, 1, 'published', 'Synthetic ' || value, 'สังเคราะห์ ' || value,
       800 + (value % 19) * 100, 1000, 256, 'token', 'original', 'verified', 'int main() {}',
       repeat('a', 64)
FROM generate_series(1, 20000) AS value;

INSERT INTO task_topics (task_id, task_version, topic)
SELECT 'synthetic-' || value, 1, 'io' FROM generate_series(1, 20000) AS value;

-- Attempts are spread over five hundred learners so one account holds a small slice. A single account
-- with twenty thousand rows would make a hash anti-join the better plan and the index irrelevant, which
-- would prove the opposite of what this gate is for.
INSERT INTO principals (kind, handle, status)
SELECT 'user', 'probe_' || value, 'active' FROM generate_series(0, 499) AS value;

INSERT INTO progress (account_id, task_id, task_version, status)
SELECT p.id, 'synthetic-' || g.value, 1, 'attempted'
FROM generate_series(1, 20000) AS g
JOIN principals AS p ON p.handle = 'probe_' || (g.value % 500);

INSERT INTO progress (account_id, task_id, task_version, status)
SELECT '$account', 'synthetic-' || value, 1, 'attempted'
FROM generate_series(1, 40) AS value;

ANALYZE tasks;
ANALYZE task_topics;
ANALYZE progress;
SQL

# The shape bank::next_unseen_task issues: published, carries a stage topic, inside the band, unattempted.
read -r -d '' query <<'SQL'
SELECT t.id, t.version
FROM tasks AS t
WHERE t.status = 'published'
  AND t.difficulty >= 800 AND t.difficulty <= 1200
  AND EXISTS (SELECT 1 FROM task_topics AS tt
              WHERE tt.task_id = t.id AND tt.task_version = t.version
                AND tt.topic IN ('io', 'complexity'))
  AND NOT EXISTS (SELECT 1 FROM progress AS p
                  WHERE p.account_id = '11111111-2222-4333-8444-555555555555'
                    AND p.task_id = t.id AND p.task_version = t.version)
ORDER BY t.difficulty, t.id
LIMIT 1
SQL

with_index="$(psql_db -c "EXPLAIN $query")"
if grep -q "progress_account_task_idx" <<<"$with_index"; then
  ok "the unattempted filter is served by progress_account_task_idx"
else
  bad "the unattempted filter is served by progress_account_task_idx" "$(head -3 <<<"$with_index")"
fi

psql_db -c "DROP INDEX progress_account_task_idx" >/dev/null 2>&1
without_index="$(psql_db -c "EXPLAIN $query")"
if grep -q "Seq Scan on progress" <<<"$without_index"; then
  ok "dropping the index falls back to a sequential scan"
else
  bad "dropping the index falls back to a sequential scan" "$(head -3 <<<"$without_index")"
fi

# The widened form must not be a different plan, or the falsification would only cover half the query.
widened="${query/800/600}"
widened="${widened/1200/1400}"
psql_db -c "CREATE UNIQUE INDEX progress_account_task_idx ON progress (account_id, task_id, task_version)" >/dev/null 2>&1
widened_plan="$(psql_db -c "EXPLAIN $widened")"
if grep -q "progress_account_task_idx" <<<"$widened_plan"; then
  ok "the widened band uses the same index"
else
  bad "the widened band uses the same index" "$(head -3 <<<"$widened_plan")"
fi

printf '\n%s assertion(s) behaved, %s did not\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
