#!/usr/bin/env bash
# Proves the models page: an operator adds a record, the key is written sealed and never read back, a second record can be
# picked as the default, and a session without the capability or without the form token is refused.
#
# It skips (exit 77) when the PostgreSQL server binaries are not installed, so a checkout without them reports a skip
# rather than a failure.
set -uo pipefail

binary="${1:?usage: model_admin_e2e.sh <path to cms_api>}"
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

# shellcheck source=../../tools/postgres-cluster.sh
. "$repo/tools/postgres-cluster.sh"

pg_cluster_binaries
for tool in initdb pg_ctl psql curl; do
  command -v "$tool" >/dev/null 2>&1 || { echo "  skipped: $tool is not installed"; exit 77; }
done

if [ ! -f "$repo/deploy/roles.sql" ] || [ ! -f "$repo/tools/apply-plan.sh" ]; then
  echo "  skipped: this tree has no deploy/ or tools/ to apply a schema from"
  exit 77
fi

work="$(mktemp -d)"
port=$((55500 + (RANDOM % 400)))
public_port=$((18000 + (RANDOM % 400)))
panel_port=$((18500 + (RANDOM % 400)))
database=cmslearn_model_probe
writer=aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeee21
outsider=aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeee22
writer_session=aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeee31
outsider_session=aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeee32

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

psql_db <<SQL >/dev/null || { echo "  [!!] fixture insert failed"; exit 1; }
INSERT INTO admin_accounts (id, handle, status)
  VALUES ('$writer', 'model-writer', 'active'), ('$outsider', 'model-outsider', 'active');
INSERT INTO roles (id, name) VALUES ('aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeee41', 'model_operator');
INSERT INTO role_capabilities (role_id, capability)
  VALUES ('aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeee41', 'settings:write'),
         ('aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeee41', 'settings:read');
INSERT INTO admin_roles (admin_id, role_id) VALUES ('$writer', 'aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeee41');
INSERT INTO sessions (id, account_kind, admin_id, expires_at)
  VALUES ('$writer_session', 'admin', '$writer', now() + interval '1 hour'),
         ('$outsider_session', 'admin', '$outsider', now() + interval '1 hour');
SQL

cat >"$work/host.env" <<ENV
CMSLEARN_LISTEN_ADDRESS=127.0.0.1
CMSLEARN_LISTEN_PORT=$public_port
CMSLEARN_PANEL_LISTEN_ADDRESS=127.0.0.1
CMSLEARN_PANEL_LISTEN_PORT=$panel_port
CMSLEARN_DATABASE_URL=host=$work port=$port dbname=$database user=postgres
CMSLEARN_SEAL_KEY=0123456789abcdef0123456789abcdef
CMSLEARN_BOOTSTRAP_ADMIN=operator@example.org
CMSLEARN_TLS_MODE=proxy
CMSLEARN_ADMIN_SOCKET=$work/admin.sock
CMSLEARN_ADMIN_UID=$(id -u)
CMSLEARN_TEMPLATE_ROOT=$repo/api/templates
CMSLEARN_STATIC_ROOT=$repo/web/public
ENV

token="$(psql_db -c "SELECT token FROM csrf_tokens LIMIT 1" 2>/dev/null)"
CMSLEARN_HOST_FILE="$work/host.env" "$binary" >"$work/api.log" 2>&1 &
api_pid=$!
sleep 2

base="http://127.0.0.1:$panel_port"
page="$(curl -s --max-time 20 -H "Cookie: cms_session=$writer_session" "$base/admin/models")"
if [[ "$page" == *"No model record yet"* ]]; then
  ok "an empty table is shown as the operator's work"
else
  bad "an empty table is shown as the operator's work" "the page has no empty notice"
fi

# The form token is issued by the page itself, so the test uses the one the page carries.
csrf="$(printf '%s' "$page" | grep -o 'name="csrf_token" value="[^"]*"' | head -1 | sed 's/.*value="//; s/"$//')"
if [ -n "$csrf" ]; then
  ok "the page issues a form token"
else
  bad "the page issues a form token" "no csrf_token input in the page"
fi

add_model() {
  curl -s -o /dev/null -w '%{http_code}' --max-time 20 -X POST "$base/admin/models" \
    -H "Cookie: cms_session=$writer_session" \
    --data-urlencode "csrf_token=$csrf" --data-urlencode "verb=add" \
    --data-urlencode "name=$1" --data-urlencode "base_url=$2" \
    --data-urlencode "protocol=openai_chat" --data-urlencode "model_id=$3" \
    --data-urlencode "key=$4" --data-urlencode "is_default=$5"
}

status="$(add_model first https://model.invalid/v1 m-first hunter-one true)"
[ "$status" = "303" ] && ok "an operator adds a record" || bad "an operator adds a record" "HTTP $status"

stored="$(psql_db -c "SELECT sealed_key FROM models WHERE name = 'first'")"
case "$stored" in
  sealed:v1:*) ok "the key reached the row sealed" ;;
  *) bad "the key reached the row sealed" "stored: ${stored:-nothing}" ;;
esac
case "$stored" in
  *hunter-one*) bad "the row carries no part of the key" "the row repeats the key" ;;
  *) ok "the row carries no part of the key" ;;
esac

page="$(curl -s --max-time 20 -H "Cookie: cms_session=$writer_session" "$base/admin/models")"
case "$page" in
  *first*) ok "the record is listed" ;;
  *) bad "the record is listed" "the page does not name it" ;;
esac
case "$page" in
  *hunter-one*) bad "the page renders no part of the key" "the page repeats the key" ;;
  *) ok "the page renders no part of the key" ;;
esac

status="$(add_model second https://other.invalid/v1 m-second hunter-two true)"
[ "$status" = "303" ] && ok "a second record can be added" || bad "a second record can be added" "HTTP $status"

defaults="$(psql_db -c "SELECT count(*) FROM models WHERE is_default")"
[ "$defaults" = "1" ] && ok "exactly one record is the default" \
  || bad "exactly one record is the default" "$defaults default(s)"

current="$(psql_db -c "SELECT name FROM models WHERE is_default")"
[ "$current" = "second" ] && ok "the second record became the effective one" \
  || bad "the second record became the effective one" "default is: ${current:-none}"

status="$(curl -s -o /dev/null -w '%{http_code}' --max-time 20 -X POST "$base/admin/models" \
  -H "Cookie: cms_session=$writer_session" --data-urlencode "csrf_token=$csrf" \
  --data-urlencode "verb=disable" --data-urlencode "model=second")"
enabled="$(psql_db -c "SELECT enabled FROM models WHERE name = 'second'")"
[ "$status" = "303" ] && [ "$enabled" = "f" ] && ok "an operator disables the effective record" \
  || bad "an operator disables the effective record" "HTTP $status, enabled=$enabled"

status="$(curl -s -o /dev/null -w '%{http_code}' --max-time 20 -X POST "$base/admin/models" \
  -H "Cookie: cms_session=$writer_session" --data-urlencode "csrf_token=$csrf" \
  --data-urlencode "verb=default" --data-urlencode "model=first")"
current="$(psql_db -c "SELECT name FROM models WHERE is_default")"
enabled="$(psql_db -c "SELECT enabled FROM models WHERE name = 'first'")"
[ "$status" = "303" ] && [ "$current" = "first" ] && [ "$enabled" = "t" ] \
  && ok "picking another record makes it the effective one" \
  || bad "picking another record makes it the effective one" "HTTP $status, default=$current, enabled=$enabled"

status="$(curl -s -o /dev/null -w '%{http_code}' --max-time 20 -X POST "$base/admin/models" \
  --data-urlencode "verb=add" --data-urlencode "name=third" --data-urlencode "base_url=https://x.invalid" \
  --data-urlencode "protocol=openai_chat" --data-urlencode "model_id=m" --data-urlencode "key=k")"
count="$(psql_db -c "SELECT count(*) FROM models")"
# No session is a redirect to the sign-in page, the same answer every panel form gives; what matters is that no row moved.
[ "$status" = "303" ] && [ "$count" = "2" ] && ok "a request with no session is refused and writes nothing" \
  || bad "a request with no session is refused and writes nothing" "HTTP $status, rows=$count"

status="$(curl -s -o /dev/null -w '%{http_code}' --max-time 20 -X POST "$base/admin/models" \
  -H "Cookie: cms_session=$outsider_session" --data-urlencode "csrf_token=$csrf" \
  --data-urlencode "verb=add" --data-urlencode "name=third" --data-urlencode "base_url=https://x.invalid" \
  --data-urlencode "protocol=openai_chat" --data-urlencode "model_id=m" --data-urlencode "key=k")"
count="$(psql_db -c "SELECT count(*) FROM models")"
[ "$status" = "403" ] && [ "$count" = "2" ] && ok "a session without the capability is refused and writes nothing" \
  || bad "a session without the capability is refused and writes nothing" "HTTP $status, rows=$count"

status="$(curl -s -o /dev/null -w '%{http_code}' --max-time 20 -X POST "$base/admin/models" \
  -H "Cookie: cms_session=$writer_session" --data-urlencode "csrf_token=not-a-token" \
  --data-urlencode "verb=disable" --data-urlencode "model=first")"
enabled="$(psql_db -c "SELECT enabled FROM models WHERE name = 'first'")"
[ "$status" = "403" ] && [ "$enabled" = "t" ] && ok "a form without its token is refused" \
  || bad "a form without its token is refused" "HTTP $status, enabled=$enabled"

printf '\n%s assertion(s) behaved, %s did not\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
