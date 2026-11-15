#!/usr/bin/env bash
# Drives the learner's progress page through the real route: anonymous is sent to sign in, an administrator is
# refused, and a learner sees the numbers their own rows support.
#
# The attempt is made by the route that makes it — POST /tasks/{id}/submit — and only then read: a page proven
# against a table the test seeded itself would prove the page and not the product.
#
# It skips (exit 77) when the PostgreSQL server binaries or the schema are not present.
set -uo pipefail

binary="${1:?usage: progress_route_e2e.sh <path to cms_api>}"
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
seeder="${2:-$(cd "$(dirname "$binary")/.." && pwd)/bank/cms_seed_content}"

# shellcheck source=../../tools/postgres-cluster.sh
. "$repo/tools/postgres-cluster.sh"

pg_cluster_binaries
for tool in initdb pg_ctl psql curl; do
  command -v "$tool" >/dev/null 2>&1 || { echo "  skipped: $tool is not installed"; exit 77; }
done
if [ ! -x "$seeder" ]; then
  echo "  skipped: this tree has no cms_seed_content at $seeder"
  exit 77
fi
if [ ! -f "$repo/deploy/roles.sql" ] || [ ! -f "$repo/tools/apply-plan.sh" ]; then
  echo "  skipped: this tree has no deploy/ or tools/ to apply a schema from"
  exit 77
fi

work="$(mktemp -d)"
port=$((58900 + (RANDOM % 80)))
listen_port=$((23400 + (RANDOM % 200)))
database=cmslearn_progress_probe
learner="aaaaaaaa-0000-4000-8000-0000000000d1"
session="aaaaaaaa-0000-4000-8000-0000000000d2"
admin="aaaaaaaa-0000-4000-8000-0000000000d3"
admin_session="aaaaaaaa-0000-4000-8000-0000000000d4"

cleanup() {
  [ -n "${api_pid:-}" ] && kill -9 "$api_pid" 2>/dev/null
  pg_cluster_stop "$work" >/dev/null 2>&1 || true
  rm -rf "$work"
}
trap cleanup EXIT

pass=0
fail=0
ok() { printf '  [ok]   %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  [!!]   %s — %s\n' "$1" "$2"; fail=$((fail + 1)); }

psql_db() { psql -X -h "$work" -p "$port" -U postgres -d "$database" -tAq -v ON_ERROR_STOP=1 "$@"; }

pg_cluster_prepare "$work" || { echo "  [!!] the cluster cannot be handed to the postgres user"; exit 1; }
pg_cluster_init "$work" >"$work/initdb.log" 2>&1 || { echo "  [!!] initdb failed"; exit 1; }
pg_cluster_start "$work" "$port" "$work/pg.log" >/dev/null 2>&1 || { echo "  [!!] pg_ctl failed"; exit 1; }
psql -X -h "$work" -p "$port" -U postgres -q -v ON_ERROR_STOP=1 -f "$repo/deploy/roles.sql" >/dev/null 2>&1
psql -X -h "$work" -p "$port" -U postgres -q -c "CREATE DATABASE $database" >/dev/null 2>&1
PGHOST="$work" PGPORT="$port" PGDATABASE="$database" PGUSER=postgres \
  bash "$repo/tools/apply-plan.sh" >"$work/apply.log" 2>&1 \
  || { echo "  [!!] apply-plan failed"; tail -5 "$work/apply.log"; exit 1; }

CMSLEARN_DATABASE_URL="host=$work port=$port dbname=$database user=postgres" \
  timeout 120 "$seeder" --content "$repo/content" >"$work/seed.log" 2>&1 \
  || { echo "  [!!] the seeder failed"; tail -5 "$work/seed.log"; exit 1; }

psql_db <<SQL >/dev/null || { echo "  [!!] fixture insert failed"; exit 1; }
INSERT INTO learner_accounts (id, handle, status) VALUES ('$learner', 'progress-probe', 'active');
INSERT INTO sessions (id, account_kind, learner_id, expires_at)
  VALUES ('$session', 'learner', '$learner', now() + interval '1 hour');
INSERT INTO admin_accounts (id, handle, status) VALUES ('$admin', 'progress-admin', 'active');
INSERT INTO sessions (id, account_kind, admin_id, expires_at)
  VALUES ('$admin_session', 'admin', '$admin', now() + interval '1 hour');
INSERT INTO tasks (id, version, status, title_en, title_th, difficulty, time_limit_ms,
                   memory_limit_mb, checker, origin, verification, solution, content_hash)
VALUES ('probe-task', 1, 'published', 'Probe', 'ทดสอบ', 1200, 1000, 256, 'token', 'original',
        'verified', 'int main() {}', repeat('a', 64));
INSERT INTO task_topics (task_id, task_version, topic) VALUES ('probe-task', 1, 'io');
INSERT INTO task_visibility (task_id, principal_id) VALUES ('probe-task', 0);
SQL

cat >"$work/host.env" <<ENV
CMSLEARN_LISTEN_ADDRESS=127.0.0.1
CMSLEARN_LISTEN_PORT=$listen_port
CMSLEARN_DATABASE_URL=host=$work port=$port dbname=$database user=postgres
CMSLEARN_SEAL_KEY=0123456789abcdef0123456789abcdef
CMSLEARN_BOOTSTRAP_ADMIN=operator@example.org
CMSLEARN_TLS_MODE=proxy
CMSLEARN_ADMIN_SOCKET=$work/admin.sock
CMSLEARN_ADMIN_UID=$(id -u)
CMSLEARN_TEMPLATE_ROOT=$repo/api/templates
CMSLEARN_STATIC_ROOT=$repo/web/public
ENV

CMSLEARN_HOST_FILE="$work/host.env" "$binary" >"$work/api.log" 2>&1 &
api_pid=$!
for _ in $(seq 1 40); do
  sleep 0.2
  kill -0 "$api_pid" 2>/dev/null || break
  curl -sf -o /dev/null --max-time 1 "http://127.0.0.1:$listen_port/healthz" && break
done
if ! kill -0 "$api_pid" 2>/dev/null; then
  bad "the api started" "$(tail -3 "$work/api.log")"
  printf '\n%s assertion(s) behaved, %s did not\n' "$pass" "$fail"
  exit 1
fi

get() {  # get <path> [cookie]
  local path="$1" cookie="${2:-}" header=()
  [ -n "$cookie" ] && header=(-H "Cookie: cms_session=$cookie")
  curl -s -o "$work/body" -D "$work/headers" -w '%{http_code}' --max-time 20 \
    "${header[@]}" "http://127.0.0.1:$listen_port$path"
}

status="$(get /progress)"
if [ "$status" = "303" ] && grep -qi '^location: /signin' "$work/headers"; then
  ok "the page without a session is sent to sign in"
else
  bad "the page without a session is sent to sign in" "HTTP $status"
fi

status="$(get /progress "$admin_session")"
[ "$status" = "403" ] && ok "an administrator is refused" || bad "an administrator is refused" "HTTP $status"

status="$(get "/progress?lang=en" "$session")"
if [ "$status" = "200" ] && grep -q "No submissions yet" "$work/body"; then
  ok "a learner with no submissions sees the empty state"
else
  bad "a learner with no submissions sees the empty state" "HTTP $status"
fi
grep -q "sets a threshold" "$work/body" && ok "the page states that no threshold exists" \
  || bad "the page states that no threshold exists" "the note is missing"

before="$(psql_db -c "SELECT count(*) FROM progress")"
status="$(curl -s -o /dev/null -w '%{http_code}' --max-time 20 -X POST \
  "http://127.0.0.1:$listen_port/tasks/probe-task/submit" \
  --data-urlencode "source=int main() { return 0; }" -H "Cookie: cms_session=$session")"
[ "$status" = "303" ] && ok "the submit route accepts the program" \
  || bad "the submit route accepts the program" "HTTP $status"

rows="$(psql_db -c "SELECT count(*) FROM progress WHERE account_id = '$learner'")"
attempted="$(psql_db -c "SELECT status FROM progress WHERE account_id = '$learner'")"
if [ "$before" = "0" ] && [ "$rows" = "1" ] && [ "$attempted" = "attempted" ]; then
  ok "the submit route recorded exactly one attempt row"
else
  bad "the submit route recorded exactly one attempt row" \
    "before $before, rows $rows, status ${attempted:-none}"
fi

# A second submission must not create a second row, and must not move attempted_at.
first_attempt="$(psql_db -c "SELECT attempted_at FROM progress WHERE account_id = '$learner'")"
curl -s -o /dev/null --max-time 20 -X POST "http://127.0.0.1:$listen_port/tasks/probe-task/submit" \
  --data-urlencode "source=int main() { return 1; }" -H "Cookie: cms_session=$session"
rows="$(psql_db -c "SELECT count(*) FROM progress WHERE account_id = '$learner'")"
second_attempt="$(psql_db -c "SELECT attempted_at FROM progress WHERE account_id = '$learner'")"
if [ "$rows" = "1" ] && [ "$first_attempt" = "$second_attempt" ]; then
  ok "a second submission keeps one row and does not move attempted_at"
else
  bad "a second submission keeps one row and does not move attempted_at" \
    "rows $rows, attempted_at $first_attempt -> $second_attempt"
fi

status="$(get "/progress?lang=en" "$session")"
if [ "$status" = "200" ] && grep -q "Attempted" "$work/body" && grep -q ">1<" "$work/body"; then
  ok "the page shows the attempt its own route recorded"
else
  bad "the page shows the attempt its own route recorded" "HTTP $status"
fi
grep -q "Untried" "$work/body" && ok "a topic never attempted reads as untried" \
  || bad "a topic never attempted reads as untried" "no untried label"

printf '\n%s assertion(s) behaved, %s did not\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
