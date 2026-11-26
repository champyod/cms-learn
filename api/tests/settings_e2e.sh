#!/usr/bin/env bash
# Proves the api serves what it claims against a real cluster, and that a settings change reaches a
# running process without a restart.
#
# This is the only test that exercises the listener, the version backstop and the two routes together.
# It skips (exit 77) when the PostgreSQL server binaries are not installed, so a checkout without them
# reports a skip rather than a failure.
set -uo pipefail

binary="${1:?usage: settings_e2e.sh <path to cms_api>}"
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

# shellcheck source=../../tools/postgres-cluster.sh
. "$repo/tools/postgres-cluster.sh"

pg_cluster_binaries
for tool in initdb pg_ctl psql curl nc; do
  command -v "$tool" >/dev/null 2>&1 || { echo "  skipped: $tool is not installed"; exit 77; }
done

# The falsification harness stages a tree without deploy/ or tools/, so this test cannot run there.
# Skipping with a reason is honest; failing would report a defect the tree does not have.
if [ ! -f "$repo/deploy/roles.sql" ] || [ ! -f "$repo/tools/apply-plan.sh" ]; then
  echo "  skipped: this tree has no deploy/ or tools/ to apply a schema from"
  exit 77
fi

work="$(mktemp -d)"
port=$((55000 + (RANDOM % 500)))
listen_port=$((18000 + (RANDOM % 500)))
database=cmslearn_api_probe

cleanup() {
  [ -n "${api_pid:-}" ] && kill "$api_pid" 2>/dev/null
  pg_cluster_stop "$work" >/dev/null 2>&1 || true
  rm -rf "$work"
}
trap cleanup EXIT

pass=0
fail=0
ok() { printf '  [ok]   %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  [!!]   %s — %s\n' "$1" "$2"; fail=$((fail + 1)); }

psql_db() { psql -X -h "$work" -p "$port" -U postgres -d "$database" -q -v ON_ERROR_STOP=1 "$@"; }

# Every settings write names its actor, kind and id; the schema raises otherwise, which is the intended barrier.
as_actor() {
  psql_db -c "BEGIN" \
           -c "SET LOCAL cms.actor_id = '$actor_id'" \
           -c "SET LOCAL cms.actor_kind = 'admin'" \
           -c "$1" \
           -c "COMMIT"
}

pg_cluster_prepare "$work" || { echo "  [!!] the cluster cannot be handed to the postgres user"; exit 1; }
pg_cluster_init "$work" >"$work/initdb.log" 2>&1 || { echo "  [!!] initdb failed"; exit 1; }
pg_cluster_start "$work" "$port" "$work/pg.log" >/dev/null 2>&1 || { echo "  [!!] pg_ctl failed"; exit 1; }

# The scratch cluster is built the way the deployment builds one: the plan's owner role exists before
# apply-plan runs, so a role the schema references is never missing.
pg_cluster_create_owner "$repo" "$work" "$port" \
  || { echo "  [!!] the deployment owner role cannot be created"; exit 1; }
psql -X -h "$work" -p "$port" -U postgres -q -v ON_ERROR_STOP=1 -f "$repo/deploy/roles.sql" >/dev/null 2>&1 \
  || { echo "  [!!] deploy/roles.sql failed"; exit 1; }
psql -X -h "$work" -p "$port" -U postgres -q -c "CREATE DATABASE $database" >/dev/null 2>&1
PGHOST="$work" PGPORT="$port" PGDATABASE="$database" PGUSER=postgres \
  bash "$repo/tools/apply-plan.sh" >"$work/apply.log" 2>&1 \
  || { echo "  [!!] apply-plan failed"; tail -5 "$work/apply.log"; exit 1; }

psql_db -c "INSERT INTO admin_accounts (handle, status) VALUES ('probe-actor', 'active')" \
  || { echo "  [!!] administrator insert failed"; exit 1; }
actor_id="$(psql_db -tAc "SELECT id FROM admin_accounts WHERE handle = 'probe-actor'")"

as_actor "INSERT INTO settings (key, kind, value_text, description)
          VALUES ('site.name', 'text', 'CMS Learn', 'probe');" \
  || { echo "  [!!] settings insert failed"; exit 1; }

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

before_version="$(psql_db -tAc 'SELECT version FROM settings_meta')"

CMSLEARN_HOST_FILE="$work/host.env" "$binary" >"$work/api.log" 2>&1 &
api_pid=$!
sleep 2

if ! kill -0 "$api_pid" 2>/dev/null; then
  bad "the api started" "$(tail -3 "$work/api.log")"
else
  ok "the api started"
fi

health="$(curl -fsS --max-time 20 "http://127.0.0.1:$listen_port/healthz" 2>&1)"
if [[ "$health" == *"ok settings-version=$before_version"* ]]; then
  ok "/healthz reports the settings version"
else
  bad "/healthz reports the settings version" "got: $health"
fi

metrics="$(curl -fsS --max-time 20 "http://127.0.0.1:$listen_port/metrics" 2>&1)"
if [[ "$metrics" == *"cms_learn_settings_version $before_version"* ]]; then
  ok "/metrics exposes the version as a gauge"
else
  bad "/metrics exposes the version as a gauge" "got: $metrics"
fi

if [ -S "$work/admin.sock" ]; then
  ok "the admin socket is bound"
else
  bad "the admin socket is bound" "no socket at $work/admin.sock"
fi

socket_mode="$(stat -c '%a' "$work/admin.sock" 2>/dev/null)"
if [ "$socket_mode" = "600" ]; then
  ok "the admin socket is 0600"
else
  bad "the admin socket is 0600" "mode is ${socket_mode:-missing}"
fi

reply="$(printf '{"action":"ping"}\n' | nc -U -N "$work/admin.sock" 2>/dev/null | head -1)"
if [[ "$reply" == *'"ok":true'* && "$reply" == *"settings_version"* ]]; then
  ok "the admin socket answers ping"
else
  bad "the admin socket answers ping" "got: ${reply:-nothing}"
fi

public_status="$(curl -s -o /dev/null -w '%{http_code}' --max-time 20 "http://127.0.0.1:$listen_port/admin/ping" 2>&1)"
if [ "$public_status" = "404" ]; then
  ok "the public listener carries no admin route"
else
  bad "the public listener carries no admin route" "got HTTP $public_status"
fi

as_actor "UPDATE settings SET value_text = 'CMS Learn (edited)' WHERE key = 'site.name';" \
  || { echo "  [!!] settings update failed"; exit 1; }
after_version="$(psql_db -tAc 'SELECT version FROM settings_meta')"
if [ "$after_version" = "$before_version" ]; then
  bad "editing a setting moves the version" "the trigger did not bump it"
else
  ok "editing a setting moves the version ($before_version -> $after_version)"
fi

propagated=""
for _ in $(seq 1 25); do
  health="$(curl -fsS --max-time 20 "http://127.0.0.1:$listen_port/healthz" 2>&1)"
  if [[ "$health" == *"settings-version=$after_version"* ]]; then propagated="$health"; break; fi
  sleep 0.2
done
if [ -n "$propagated" ]; then
  ok "the listener propagated the change without a restart"
else
  bad "the listener propagated the change without a restart" "still: $health"
fi


# This case carries its own learner and task: the file seeds neither elsewhere, and a submission needs both.
psql_db -c "INSERT INTO tasks (id, version, status, title_en, title_th, difficulty, time_limit_ms, memory_used_limit_mb, memory_reserved_limit_mb, checker, origin, verification, solution, content_hash) VALUES ('metrics-probe', 1, 'draft', 'Metrics probe', 'ตัววัด', 900, 1000, 256, 512, 'token', 'original', 'pending', '', '')"
psql_db -c "INSERT INTO learner_accounts (id, handle, status) VALUES ('e0000001-1111-4111-8111-000000000001', 'metrics_probe', 'active')"
psql_db -c "INSERT INTO submissions (account_id, task_id, task_version, source, submitted_at) VALUES ('e0000001-1111-4111-8111-000000000001', 'metrics-probe', 1, 'print(1)', now() - interval '30 seconds')"
psql_db -c "INSERT INTO submissions (account_id, task_id, task_version, source, verdict, judged_at) VALUES ('e0000001-1111-4111-8111-000000000001', 'metrics-probe', 1, 'print(2)', 'ok', now())"

metrics="$(curl -fsS --max-time 20 "http://127.0.0.1:$listen_port/metrics" 2>&1)"
depth="$(sed -n 's/^cms_learn_judge_queue_depth //p' <<<"$metrics")"
oldest="$(sed -n 's/^cms_learn_judge_oldest_waiting_seconds //p' <<<"$metrics")"
if [ "$depth" = "1" ]; then
  ok "/metrics counts the waiting submissions"
else
  bad "/metrics counts the waiting submissions" "got: ${depth:-missing}"
fi
case "$oldest" in
  29|30|31|32|33) ok "/metrics ages the oldest waiting submission (${oldest}s)" ;;
  *) bad "/metrics ages the oldest waiting submission" "got: ${oldest:-missing}" ;;
esac

# A scrape target answers when a subsystem it reports on cannot be read: hide the table the queue
# aggregates, so exactly that read fails while the rest of the database and the service stay up.
psql_db -c "ALTER TABLE submissions RENAME TO submissions_metrics_probe" >/dev/null 2>&1 \
  || { echo "  [!!] the queue table could not be hidden"; exit 1; }
status="$(curl -s -o "$work/metrics-down" -w '%{http_code}' --max-time 20 "http://127.0.0.1:$listen_port/metrics")"
down="$(cat "$work/metrics-down" 2>/dev/null || true)"
if [ "$status" = "200" ]; then
  ok "/metrics answers when the queue cannot be read"
else
  bad "/metrics answers when the queue cannot be read" "got HTTP $status"
fi
case "$down" in
  *"cms_learn_judge_queue_depth NaN"*) ok "/metrics reports the queue depth as NaN" ;;
  *) bad "/metrics reports the queue depth as NaN" "got: ${down:-nothing}" ;;
esac
case "$down" in
  *"cms_learn_judge_oldest_waiting_seconds NaN"*) ok "/metrics reports the oldest wait as NaN" ;;
  *) bad "/metrics reports the oldest wait as NaN" "got: ${down:-nothing}" ;;
esac
case "$down" in
  *"cms_learn_settings_version $after_version"*) ok "the other gauges survive the failed read" ;;
  *) bad "the other gauges survive the failed read" "got: ${down:-nothing}" ;;
esac
psql_db -c "ALTER TABLE submissions_metrics_probe RENAME TO submissions" >/dev/null 2>&1 || true

printf '\n%s assertion(s) behaved, %s did not\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
