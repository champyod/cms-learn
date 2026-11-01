#!/usr/bin/env bash
# Posts a submission through the real route and checks the row it left.
#
# It skips (exit 77) when the PostgreSQL server binaries are not installed.
set -uo pipefail

binary="${1:?usage: submit_e2e.sh <path to cms_api>}"
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

for tool in initdb pg_ctl psql curl; do
  command -v "$tool" >/dev/null 2>&1 || { echo "  skipped: $tool is not installed"; exit 77; }
done

work="$(mktemp -d)"
port=$((57000 + (RANDOM % 400)))
listen_port=$((19000 + (RANDOM % 400)))
database=cmslearn_submit_probe
account="aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeee01"
session="aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeee02"

cleanup() {
  [ -n "${api_pid:-}" ] && kill "$api_pid" 2>/dev/null
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
psql -X -h "$work" -p "$port" -U postgres -q -v ON_ERROR_STOP=1 -f "$repo/deploy/roles.sql" >/dev/null 2>&1
psql -X -h "$work" -p "$port" -U postgres -q -c "CREATE DATABASE $database" >/dev/null 2>&1
PGHOST="$work" PGPORT="$port" PGDATABASE="$database" PGUSER=postgres \
  bash "$repo/tools/apply-plan.sh" >"$work/apply.log" 2>&1 \
  || { echo "  [!!] apply-plan failed"; tail -5 "$work/apply.log"; exit 1; }

# A learner, a live session and a published task: the three facts the route needs.
psql_db <<SQL >/dev/null 2>&1
INSERT INTO principals (id, kind, handle, status) VALUES ('$account', 'user', 'submitter', 'active');
INSERT INTO sessions (id, principal_id, expires_at) VALUES ('$session', '$account', now() + interval '1 hour');
INSERT INTO tasks (id, version, status, title_en, title_th, difficulty, time_limit_ms,
                   memory_limit_mb, checker, origin, verification, solution, content_hash)
VALUES ('probe-task', 1, 'published', 'Probe', 'ทดสอบ', 900, 1000, 256, 'token', 'original',
        'verified', 'int main() {}', repeat('a', 64));
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
ENV

CMSLEARN_HOST_FILE="$work/host.env" "$binary" >"$work/api.log" 2>&1 &
api_pid=$!
sleep 2

if ! kill -0 "$api_pid" 2>/dev/null; then
  bad "the api started" "$(tail -3 "$work/api.log")"
  printf '\n%s assertion(s) behaved, %s did not\n' "$pass" "$fail"
  exit 1
fi

reply="$(curl -s --max-time 5 -w '\n%{http_code}' \
  -X POST "http://127.0.0.1:$listen_port/tasks/probe-task/submit" \
  --data-urlencode "source=int main() { return 0; }" \
  -H "Cookie: cms_session=$session")"
status="$(printf '%s' "$reply" | tail -1)"
[ "$status" = "303" ] && ok "the submission is accepted and redirected" \
  || bad "the submission is accepted and redirected" "HTTP $status: $(printf '%s' "$reply" | head -1)"

stored="$(psql_db -c "SELECT count(*) FROM submissions WHERE account_id = '$account' AND task_id = 'probe-task'")"
[ "$stored" = "1" ] && ok "the submission row exists and names the account from the session" \
  || bad "the submission row exists" "found $stored row(s)"

body="$(psql_db -c "SELECT source FROM submissions WHERE account_id = '$account'")"
[ "$body" = "int main() { return 0; }" ] && ok "the program survived the form encoding" \
  || bad "the program survived the form encoding" "stored: $body"

# Without the cookie the route must refuse rather than write a row for nobody.
status="$(curl -s -o /dev/null -w '%{http_code}' --max-time 5 \
  -X POST "http://127.0.0.1:$listen_port/tasks/probe-task/submit" --data "source=x")"
[ "$status" = "400" ] && ok "a submission with no session is refused" \
  || bad "a submission with no session is refused" "HTTP $status"
count="$(psql_db -c "SELECT count(*) FROM submissions")"
[ "$count" = "1" ] && ok "the refused submission wrote no row" \
  || bad "the refused submission wrote no row" "$count row(s)"

printf '\n%s assertion(s) behaved, %s did not\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
