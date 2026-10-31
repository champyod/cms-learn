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

for tool in initdb pg_ctl psql curl; do
  command -v "$tool" >/dev/null 2>&1 || { echo "  skipped: $tool is not installed"; exit 77; }
done

work="$(mktemp -d)"
port=$((55000 + (RANDOM % 500)))
listen_port=$((18000 + (RANDOM % 500)))
database=cmslearn_api_probe

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

psql_db() { psql -X -h "$work" -p "$port" -U postgres -d "$database" -q -v ON_ERROR_STOP=1 "$@"; }

# Every settings write names its actor; the schema raises otherwise, which is the intended barrier.
as_actor() {
  psql_db -c "BEGIN" \
           -c "SET LOCAL cms.actor_id = '$actor_id'" \
           -c "$1" \
           -c "COMMIT"
}

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

psql_db -c "INSERT INTO principals (kind, handle, status) VALUES ('system', 'probe-actor', 'active')" \
  || { echo "  [!!] principal insert failed"; exit 1; }
actor_id="$(psql_db -tAc "SELECT id FROM principals WHERE handle = 'probe-actor'")"

as_actor "INSERT INTO settings (key, kind, value_text, updated_by, description)
          VALUES ('site.name', 'text', 'CMS Learn',
                  (SELECT id FROM principals WHERE handle = 'probe-actor'), 'probe');" \
  || { echo "  [!!] settings insert failed"; exit 1; }

cat >"$work/host.env" <<ENV
CMSLEARN_LISTEN_ADDRESS=127.0.0.1
CMSLEARN_LISTEN_PORT=$listen_port
CMSLEARN_DATABASE_URL=host=$work port=$port dbname=$database user=postgres
CMSLEARN_SEAL_KEY=0123456789abcdef0123456789abcdef
CMSLEARN_BOOTSTRAP_ADMIN=operator@example.org
CMSLEARN_TLS_MODE=proxy
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

health="$(curl -fsS --max-time 5 "http://127.0.0.1:$listen_port/healthz" 2>&1)"
if [[ "$health" == *"ok settings-version=$before_version"* ]]; then
  ok "/healthz reports the settings version"
else
  bad "/healthz reports the settings version" "got: $health"
fi

metrics="$(curl -fsS --max-time 5 "http://127.0.0.1:$listen_port/metrics" 2>&1)"
if [[ "$metrics" == *"cms_learn_settings_version $before_version"* ]]; then
  ok "/metrics exposes the version as a gauge"
else
  bad "/metrics exposes the version as a gauge" "got: $metrics"
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
  health="$(curl -fsS --max-time 5 "http://127.0.0.1:$listen_port/healthz" 2>&1)"
  if [[ "$health" == *"settings-version=$after_version"* ]]; then propagated="$health"; break; fi
  sleep 0.2
done
if [ -n "$propagated" ]; then
  ok "the listener propagated the change without a restart"
else
  bad "the listener propagated the change without a restart" "still: $health"
fi

printf '\n%s assertion(s) behaved, %s did not\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
