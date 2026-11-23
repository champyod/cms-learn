#!/usr/bin/env bash
# Drives the all-tasks page through the real route: the link the curriculum emits, the list a learner sees,
# two topics narrowing it at once, and a rung narrowing it — against a real database holding the seeded
# bank and the seeded rung list.
#
# The route is the one `home.html` already links to (`/tasks?track=&stage=`), so this case is what keeps
# that link honest: a route that answered nothing, served everybody, or ignored a filter fails here.
#
# It skips (exit 77) when the PostgreSQL server binaries, the schema or the content seeder are not present.
set -uo pipefail

binary="${1:?usage: task_list_route_e2e.sh <path to cms_api>}"
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
port=$((58700 + (RANDOM % 90)))
listen_port=$((23200 + (RANDOM % 200)))
database=cmslearn_task_list_probe
learner="aaaaaaaa-0000-4000-8000-0000000000c1"
session="aaaaaaaa-0000-4000-8000-0000000000c2"

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

# The scratch cluster is built the way the deployment builds one: the plan's owner role exists before
# apply-plan runs, so a role the schema references is never missing.
pg_cluster_create_owner "$repo" "$work" "$port" \
  || { echo "  [!!] the deployment owner role cannot be created"; exit 1; }
psql -X -h "$work" -p "$port" -U postgres -q -v ON_ERROR_STOP=1 -f "$repo/deploy/roles.sql" >/dev/null 2>&1
psql -X -h "$work" -p "$port" -U postgres -q -c "CREATE DATABASE $database" >/dev/null 2>&1
PGHOST="$work" PGPORT="$port" PGDATABASE="$database" PGUSER=postgres \
  bash "$repo/tools/apply-plan.sh" >"$work/apply.log" 2>&1 \
  || { echo "  [!!] apply-plan failed"; tail -5 "$work/apply.log"; exit 1; }

# The topic vocabulary and the rung list are content: the filter offers both, so both are seeded the way an
# installation seeds them, not hand-written here.
CMSLEARN_DATABASE_URL="host=$work port=$port dbname=$database user=postgres" \
  timeout 120 "$seeder" --content "$repo/content" >"$work/seed.log" 2>&1 \
  || { echo "  [!!] the seeder failed"; tail -5 "$work/seed.log"; exit 1; }

# Four published tasks: two share a topic, one carries two topics, one carries a third, and one has no
# visibility row so it must never appear. The rungs come from the seeded list (900 -> TOI-Zero,
# 1600 -> Camp 1, 2600 -> TOI-IOI).
psql_db <<SQL >/dev/null || { echo "  [!!] fixture insert failed"; exit 1; }
INSERT INTO learner_accounts (id, handle, status) VALUES ('$learner', 'list-probe-learner', 'active');
INSERT INTO sessions (id, account_kind, learner_id, expires_at)
  VALUES ('$session', 'learner', '$learner', now() + interval '1 hour');
INSERT INTO tasks (id, version, status, title_en, title_th, difficulty, time_limit_ms,
                   memory_used_limit_mb, memory_reserved_limit_mb, checker, origin, verification, solution, content_hash)
VALUES ('alpha', 1, 'draft', 'Alpha', 'แอลฟา', 900, 1000, 256, 512, 'token', 'original',
        'pending', 'int main() {}', repeat('a', 64)),
       ('beta', 1, 'draft', 'Beta', 'บีตา', 1200, 1000, 256, 512, 'token', 'original',
        'pending', 'int main() {}', repeat('b', 64)),
       ('gamma', 1, 'draft', 'Gamma', 'แกมมา', 1600, 1000, 256, 512, 'token', 'original',
        'pending', 'int main() {}', repeat('c', 64)),
       ('delta', 1, 'draft', 'Delta', 'เดลตา', 2600, 1000, 256, 512, 'token', 'original',
        'pending', 'int main() {}', repeat('d', 64)),
       ('hidden', 1, 'draft', 'Hidden', 'ซ่อน', 900, 1000, 256, 512, 'token', 'original',
        'pending', 'int main() {}', repeat('e', 64));
-- ADR 0046 §2: a task row cannot be created verified, so the fixture builds the certificate it needs and moves the
-- verification with the guarded UPDATE the trigger allows.
INSERT INTO generation_jobs (task_id, task_version)
SELECT id, version FROM tasks WHERE id IN ('alpha', 'beta', 'gamma', 'delta', 'hidden')
ON CONFLICT (task_id, task_version) DO NOTHING;
INSERT INTO generation_certificates (job_id, task_id, task_version, outcome, checks, commit_id, evidence)
SELECT j.id, t.id, t.version, 'verified', '["the task list route fixture certified this task"]'::jsonb, repeat('c', 40), '{}'::jsonb
  FROM tasks t JOIN generation_jobs j ON j.task_id = t.id AND j.task_version = t.version
 WHERE t.id IN ('alpha', 'beta', 'gamma', 'delta', 'hidden');
UPDATE tasks SET verification = 'verified' WHERE id IN ('alpha', 'beta', 'gamma', 'delta', 'hidden') AND version = 1;
-- Publishing needs the certificate that came before it (ADR 0057): the row is created draft, certified, then published.
UPDATE tasks SET status = 'published' WHERE verification = 'verified';
INSERT INTO task_topics (task_id, task_version, topic) VALUES
  ('alpha', 1, 'io'),
  ('beta', 1, 'complexity'),
  ('gamma', 1, 'io'), ('gamma', 1, 'complexity'),
  ('delta', 1, 'simulation');
INSERT INTO task_visibility (task_id, principal_id) VALUES
  ('alpha', 0), ('beta', 0), ('gamma', 0), ('delta', 0);
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
listed() { grep -q ">$1<" "$work/body"; }

status="$(get /tasks)"
if [ "$status" = "303" ] && grep -qi '^location: /signin' "$work/headers"; then
  ok "the list without a session is sent to sign in"
else
  bad "the list without a session is sent to sign in" "HTTP $status"
fi

status="$(get "/tasks?lang=en" "$session")"
if [ "$status" = "200" ] && listed Alpha && listed Beta && listed Gamma && listed Delta; then
  ok "a learner gets the list of the tasks they may see"
else
  bad "a learner gets the list of the tasks they may see" "HTTP $status: $(head -c 160 "$work/body")"
fi
listed Hidden && bad "a task with no visibility row is absent" "Hidden is listed" \
  || ok "a task with no visibility row is absent"

grep -qF 'Camp 1 (1500' "$work/body" && ok "the filter offers the rungs the seeded vocabulary defines" \
  || bad "the filter offers the rungs the seeded vocabulary defines" "no Camp 1 radio"

status="$(get "/tasks?lang=en&topic:io=on&topic:simulation=on" "$session")"
if [ "$status" = "200" ] && listed Alpha && listed Gamma && listed Delta && ! listed Beta; then
  ok "two topics at once keep the tasks carrying either of them"
else
  bad "two topics at once keep the tasks carrying either of them" "HTTP $status"
fi

status="$(get "/tasks?lang=en&level=1500-1800" "$session")"
if [ "$status" = "200" ] && listed Gamma && ! listed Alpha && ! listed Beta && ! listed Delta; then
  ok "a rung keeps only the tasks in its band"
else
  bad "a rung keeps only the tasks in its band" "HTTP $status"
fi

status="$(get "/tasks?lang=en&topic:io=on&level=2500-3500" "$session")"
if [ "$status" = "200" ] && ! listed Alpha && ! listed Gamma && ! listed Delta && ! listed Beta; then
  ok "two filters at once that nothing satisfies list nothing"
else
  bad "two filters at once that nothing satisfies list nothing" "HTTP $status"
fi

status="$(get "/tasks?lang=en&level=1500x-1800" "$session")"
[ "$status" = "400" ] && ok "a malformed band is refused rather than ignored" \
  || bad "a malformed band is refused rather than ignored" "HTTP $status"

printf '\n%s assertion(s) behaved, %s did not\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
