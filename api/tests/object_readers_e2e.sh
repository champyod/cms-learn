#!/usr/bin/env bash
# Both readers of the task bytes, against a real store: the api serves a file-kind field from object storage and
# answers 502 naming the key when the object is gone, and the seeder refuses and writes nothing when a row names
# an object the store does not hold.
#
# The store is a real S3 gateway (the `objects` image the stack runs), not a stub: the same client in core reads
# it with the same credential path a deployment uses. Skips (exit 77) when docker or the image is missing.
set -uo pipefail

binary="${1:?usage: object_readers_e2e.sh <path to cms_api>}"
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
seeder="${2:-$(cd "$(dirname "$binary")/.." && pwd)/bank/cms_seed_content}"

for tool in initdb pg_ctl createdb psql curl docker; do
  command -v "$tool" >/dev/null 2>&1 || { echo "  skipped: $tool is not installed"; exit 77; }
done
[ -x "$seeder" ] || { echo "  skipped: this tree has no cms_seed_content"; exit 77; }
docker image inspect chrislusf/seaweedfs:4.51 >/dev/null 2>&1 || {
  echo "  skipped: the objects image is not present"; exit 77; }

work="$(mktemp -d)"
port=$((64600 + (RANDOM % 40)))
s3_port=$((25500 + (RANDOM % 90)))
listen=$((25700 + (RANDOM % 90)))
panel=$((listen + 1))
container="cms-objects-probe-$RANDOM"
cleanup() {
  [ -n "${api_pid:-}" ] && kill -9 "$api_pid" 2>/dev/null
  docker rm -f "$container" >/dev/null 2>&1
  pg_ctl -D "$work/data" -m immediate stop >/dev/null 2>&1
  rm -rf "$work"
}
trap cleanup EXIT

pass=0
fail=0
ok() { printf '  [ok]   %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  [!!]   %s — %s\n' "$1" "$2"; fail=$((fail + 1)); }
psql_db() { psql -X -h "$work" -p "$port" -U postgres -d objectprobe -tAq -c "$1"; }

seed_cluster() {
  initdb -D "$work/data" -U postgres -A trust >/dev/null 2>&1
  pg_ctl -D "$work/data" -o "-c listen_addresses='' -c unix_socket_directories=$work -p $port" \
    -l "$work/pg.log" start >/dev/null 2>&1
  sleep 1
  psql -X -h "$work" -p "$port" -U postgres -q -f "$repo/deploy/roles.sql" >/dev/null 2>&1
  createdb -h "$work" -p "$port" -U postgres objectprobe
  PGHOST="$work" PGPORT="$port" PGDATABASE=objectprobe PGUSER=postgres \
    bash "$repo/tools/apply-plan.sh" >/dev/null 2>&1
}
seed_cluster

docker run -d --rm --name "$container" -p "127.0.0.1:$s3_port:8333" \
  chrislusf/seaweedfs:4.51 server -s3 -dir=/data >/dev/null 2>&1
for _ in $(seq 1 80); do
  sleep 0.5
  curl -sf -o /dev/null --max-time 2 "http://127.0.0.1:$s3_port/status" && break
  curl -s -o /dev/null --max-time 2 "http://127.0.0.1:$s3_port/" && break
done
bucket="cms-learn"
curl -s -o /dev/null -X PUT "http://127.0.0.1:$s3_port/$bucket"
printf 'อ่านจำนวนเต็มแล้วบวกกัน' >"$work/statement.txt"
curl -s -o /dev/null -X PUT --data-binary @"$work/statement.txt" \
  "http://127.0.0.1:$s3_port/$bucket/tasks/probe/statement.txt"

export CMSLEARN_S3_ENDPOINT="http://127.0.0.1:$s3_port"
export CMSLEARN_S3_REGION="us-east-1"
export CMSLEARN_S3_BUCKET="$bucket"
export CMSLEARN_S3_ACCESS_KEY_ID="probe-access-key"
export CMSLEARN_S3_SECRET_ACCESS_KEY="probe-secret-key"

# ── the seeder: refuses a row whose object is absent, writes nothing, and succeeds once it is there ──
CMSLEARN_DATABASE_URL="host=$work port=$port dbname=objectprobe user=postgres" \
  "$seeder" --content "$repo/content" >"$work/seed1.log" 2>&1
[ $? -eq 0 ] && ok "the seeder imports with a real store configured" \
  || bad "the seeder imports with a real store configured" "$(tail -2 "$work/seed1.log")"

psql_db "SELECT set_config('cms.actor_id', 'a0000001-1111-4111-8111-0000000000f9', false), set_config('cms.actor_kind', 'admin', false), set_config('cms.invoked_by', 'object_readers_e2e', false); INSERT INTO admin_accounts (id, handle, status) VALUES ('a0000001-1111-4111-8111-0000000000f9', 'objects_author', 'active') ON CONFLICT DO NOTHING;" >/dev/null
psql_db "INSERT INTO tasks (id, version, task_type, status, title_en, title_th, difficulty, time_limit_ms, memory_limit_mb, checker, origin, verification, solution, content_hash, seeded) VALUES ('probe-object', 1, 'batch', 'published', 'Object probe', 'ทดสอบวัตถุ', 900, 1000, 256, 'token', 'original', 'verified', '', '', false) ON CONFLICT DO NOTHING;" >/dev/null
psql_db "SELECT set_config('cms.actor_id', 'a0000001-1111-4111-8111-0000000000f9', false), set_config('cms.actor_kind', 'admin', false), set_config('cms.invoked_by', 'object_readers_e2e', false); INSERT INTO task_field_values (task_id, task_version, field_key, value) VALUES ('probe-object', 1, 'solution', '\"tasks/probe/absent.cpp\"') ON CONFLICT DO NOTHING;" >/dev/null

before="$(psql_db "SELECT version FROM content_state LIMIT 1" 2>/dev/null)"
CMSLEARN_DATABASE_URL="host=$work port=$port dbname=objectprobe user=postgres" \
  "$seeder" --content "$repo/content" >"$work/seed2.log" 2>&1
status=$?
after="$(psql_db "SELECT version FROM content_state LIMIT 1" 2>/dev/null)"
[ "$status" -ne 0 ] && grep -q "tasks/probe/absent.cpp" "$work/seed2.log" && [ "$before" = "$after" ] \
  && ok "the seeder refuses the missing object by key and writes nothing" \
  || bad "the seeder refuses the missing object by key and writes nothing" \
      "exit $status, version $before -> $after, $(tail -1 "$work/seed2.log")"

printf 'int main() {}' >"$work/solution.cpp"
curl -s -o /dev/null -X PUT --data-binary @"$work/solution.cpp" \
  "http://127.0.0.1:$s3_port/$bucket/tasks/probe/absent.cpp"
CMSLEARN_DATABASE_URL="host=$work port=$port dbname=objectprobe user=postgres" \
  "$seeder" --content "$repo/content" >"$work/seed3.log" 2>&1
[ $? -eq 0 ] && ok "the seeder imports once the object exists, reading it through the store" \
  || bad "the seeder imports once the object exists, reading it through the store" "$(tail -2 "$work/seed3.log")"

# ── the api: serves the bytes, and names the key when the object is gone ──
cat > "$work/host.env" <<ENV
CMSLEARN_LISTEN_ADDRESS=127.0.0.1
CMSLEARN_LISTEN_PORT=$listen
CMSLEARN_PANEL_LISTEN_ADDRESS=127.0.0.1
CMSLEARN_PANEL_LISTEN_PORT=$panel
CMSLEARN_DATABASE_URL=host=$work port=$port dbname=objectprobe user=postgres
CMSLEARN_SEAL_KEY=0123456789abcdef0123456789abcdef
CMSLEARN_BOOTSTRAP_ADMIN=objects@example.org
CMSLEARN_TLS_MODE=proxy
CMSLEARN_ADMIN_SOCKET=$work/admin.sock
CMSLEARN_ADMIN_UID=$(id -u)
CMSLEARN_TEMPLATE_ROOT=$repo/api/templates
CMSLEARN_STATIC_ROOT=$repo/web/public
CMSLEARN_S3_ENDPOINT=$CMSLEARN_S3_ENDPOINT
CMSLEARN_S3_REGION=$CMSLEARN_S3_REGION
CMSLEARN_S3_BUCKET=$CMSLEARN_S3_BUCKET
CMSLEARN_S3_ACCESS_KEY_ID=$CMSLEARN_S3_ACCESS_KEY_ID
CMSLEARN_S3_SECRET_ACCESS_KEY=$CMSLEARN_S3_SECRET_ACCESS_KEY
ENV
CMSLEARN_HOST_FILE="$work/host.env" "$binary" >"$work/api.log" 2>&1 &
api_pid=$!
for _ in $(seq 1 80); do sleep 0.25; curl -sf -o /dev/null --max-time 2 "http://127.0.0.1:$listen/healthz" && break; done
kill -0 "$api_pid" 2>/dev/null || { echo "  [!!] the api did not start: $(tail -3 "$work/api.log")"; exit 1; }

admin_id="$(psql_db "SELECT id FROM admin_accounts ORDER BY handle LIMIT 1")"
# A role of this case's own, so the assertion does not depend on when the service finished seeding.
role=b0000001-1111-4111-8111-0000000000f1
psql_db "SELECT set_config('cms.actor_id', '$admin_id', false), set_config('cms.actor_kind', 'admin', false), set_config('cms.invoked_by', 'object_readers_e2e', false); INSERT INTO roles (id, name) VALUES ('$role', 'objects_reader') ON CONFLICT DO NOTHING; INSERT INTO role_capabilities (role_id, capability) VALUES ('$role', 'content:read') ON CONFLICT DO NOTHING; INSERT INTO admin_roles (admin_id, role_id) VALUES ('$admin_id', '$role') ON CONFLICT DO NOTHING;" >/dev/null
session=aaaaaaaa-0000-4000-8000-0000000000f8
psql_db "INSERT INTO sessions (id, account_kind, admin_id, expires_at) VALUES ('$session', 'admin', '$admin_id', now() + interval '1 hour')" >/dev/null

curl -s -o "$work/body" -w '%{http_code}' --max-time 20 -H "Cookie: cms_session=$session" \
  "http://127.0.0.1:$panel/admin/tasks/probe-object/field/solution" >"$work/status"
[ "$(cat "$work/status")" = "200" ] && cmp -s "$work/body" "$work/solution.cpp" \
  && ok "the api serves a file-kind field's bytes from the store" \
  || bad "the api serves a file-kind field's bytes from the store" "HTTP $(cat "$work/status")"

curl -s -o /dev/null -X DELETE "http://127.0.0.1:$s3_port/$bucket/tasks/probe/absent.cpp"
code="$(curl -s -o "$work/body" -w '%{http_code}' --max-time 20 -H "Cookie: cms_session=$session" \
  "http://127.0.0.1:$panel/admin/tasks/probe-object/field/solution")"
[ "$code" = "502" ] && grep -q "tasks/probe/absent.cpp" "$work/body" \
  && ok "a missing object is a 502 naming the key, never empty content" \
  || bad "a missing object is a 502 naming the key, never empty content" "HTTP $code"

code="$(curl -s -o /dev/null -w '%{http_code}' --max-time 20 -H "Cookie: cms_session=$session" \
  "http://127.0.0.1:$panel/admin/tasks/probe-object/field/statement")"
[ "$code" = "404" ] && ok "a field that is not a file is not served from the store" \
  || bad "a field that is not a file is not served from the store" "HTTP $code"

printf '\n%s assertion(s) behaved, %s did not\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
