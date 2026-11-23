#!/usr/bin/env bash
# Posts a settings form through the real route and checks what it wrote, and what it refused.
#
# The settings page renders one field per *offered* key — the ones a service actually reads (ADR 0054 §5) — and each
# field states its scope, who may change it, what a change costs and whether it is stored sealed. The route is exercised
# the way a browser posts it: cookie in, form body in, row out. It skips (exit 77) when the PostgreSQL server
# binaries are not installed.
set -uo pipefail

binary="${1:?usage: settings_route_test.sh <path to cms_api>}"
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

# shellcheck source=../../tools/postgres-cluster.sh
. "$repo/tools/postgres-cluster.sh"

pg_cluster_binaries
for tool in initdb pg_ctl psql curl; do
  command -v "$tool" >/dev/null 2>&1 || { echo "  skipped: $tool is not installed"; exit 77; }
done

work="$(mktemp -d)"
port=$((57600 + (RANDOM % 400)))
listen_port=$((19600 + (RANDOM % 400)))
database=cmslearn_settings_route_probe
writer="aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeee11"
writer_session="aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeee12"
reader="aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeee13"
reader_session="aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeee14"
role="aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeee15"

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

# Two live administrator sessions: one whose role holds settings:write, and one that holds no role at all.
# Both are administrators, because a capability is reached through an administrator's roles and a learner
# holds none.
psql_db <<SQL >/dev/null || { echo "  [!!] fixture insert failed"; exit 1; }
INSERT INTO admin_accounts (id, handle, status)
  VALUES ('$writer', 'settings-writer', 'active'), ('$reader', 'settings-reader', 'active');
INSERT INTO roles (id, name) VALUES ('$role', 'settings_operator');
INSERT INTO role_capabilities (role_id, capability)
  VALUES ('$role', 'settings:write'), ('$role', 'settings:secret_write'), ('$role', 'settings:read');
INSERT INTO admin_roles (admin_id, role_id) VALUES ('$writer', '$role');
INSERT INTO sessions (id, account_kind, admin_id, expires_at)
  VALUES ('$writer_session', 'admin', '$writer', now() + interval '1 hour');
INSERT INTO sessions (id, account_kind, admin_id, expires_at)
  VALUES ('$reader_session', 'admin', '$reader', now() + interval '1 hour');
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
sleep 2

if ! kill -0 "$api_pid" 2>/dev/null; then
  bad "the api started" "$(tail -3 "$work/api.log")"
  printf '\n%s assertion(s) behaved, %s did not\n' "$pass" "$fail"
  exit 1
fi
ok "the api started"

reply="$(curl -s --max-time 20 -D "$work/headers" -w '\n%{http_code}' \
  -X POST "http://127.0.0.1:$listen_port/settings" \
  --data-urlencode "mail.password=hunter2" \
  --data-urlencode "site.name=CMS Learn Probe" \
  -H "Cookie: cms_session=$writer_session")"
status="$(printf '%s' "$reply" | tail -1)"
body="$(printf '%s' "$reply" | head -n -1)"

[ "$status" = "303" ] && ok "a catalogued form from a live session is accepted" \
  || bad "a catalogued form from a live session is accepted" "HTTP $status: $body"

if grep -qi '^location: /settings' "$work/headers"; then
  ok "the redirect names the settings page"
else
  bad "the redirect names the settings page" "$(grep -i '^location:' "$work/headers" || echo 'no Location')"
fi

case "$body" in
  *hunter2*) bad "the response carries no secret value" "the body repeats hunter2" ;;
  *) ok "the response carries no secret value" ;;
esac

if printf '%s' "$body" | grep -Eq 'password[^[:alnum:]]*hunter2'; then
  bad "the response does not echo the password field" "the body repeats password=hunter2"
else
  ok "the response does not echo the password field"
fi

# The row holds the seal, never the secret: the api seals a catalogued secret before the write, and the schema refuses a
# secret-kind row that does not start with the sealed prefix, so this is the proof of both.
stored="$(psql_db -c "SELECT value_text FROM settings WHERE key = 'mail.password'")"
case "$stored" in
  sealed:v1:*) ok "the secret reached the settings row sealed" ;;
  *) bad "the secret reached the settings row sealed" "stored: ${stored:-nothing}" ;;
esac
case "$stored" in
  *hunter2*) bad "the row carries no part of the secret" "the row repeats hunter2" ;;
  *) ok "the row carries no part of the secret" ;;
esac

# The page a person lands on says a secret is stored and that it is sealed; it never renders the value.
# The page needs settings:read, which the writing session's role now holds, and it shows one group at a time: fetch the
# mail group, where the secret is rendered.
page="$(curl -s --max-time 20 -H "Cookie: cms_session=$writer_session" "http://127.0.0.1:$listen_port/settings?group=mail")"
case "$page" in
  *sealed*) ok "the settings page renders the secret as sealed" ;;
  *) bad "the settings page renders the secret as sealed" "the page has no sealed marker" ;;
esac
case "$page" in
  *hunter2*) bad "the settings page renders no part of the secret" "the page repeats hunter2" ;;
  *) ok "the settings page renders no part of the secret" ;;
esac

# The page offers only what a service reads and states what each change costs (ADR 0054 §5).
page_site="$(curl -s --max-time 20 -H "Cookie: cms_session=$writer_session" "http://127.0.0.1:$listen_port/settings?group=site")"
case "$page_site" in
  *'name="site.name"'*) ok "a setting a service reads is offered" ;;
  *) bad "a setting a service reads is offered" "site.name is missing" ;;
esac
case "$page_site" in
  *'name="site.tagline"'*) bad "a catalogued-but-unread setting is not offered" "site.tagline is on the page" ;;
  *) ok "a catalogued-but-unread setting is not offered" ;;
esac
missing="$(printf '%s' "$page_site" | python3 -c '
import re, sys
html = sys.stdin.read()
rows = re.findall(r"<p class=\"mb-1 text-xs opacity-70\"([^>]*)>", html)
print(sum(1 for r in rows if not all(a in r for a in ("data-scope=", "data-permission=", "data-effect=", "data-sealed="))))
')"
[ "$missing" = "0" ] && [ -n "$page_site" ] && ok "every offered setting states its scope, permission, effect and whether it is sealed" \
  || bad "every offered setting states its scope, permission, effect and whether it is sealed" "$missing row(s) short a fact"
case "$page_site" in
  *"takes effect immediately"*|*"after the api service restarts"*) ok "a change states what it costs before it is made" ;;
  *) bad "a change states what it costs before it is made" "no effect sentence on the page" ;;
esac

tier="$(psql_db -c "SELECT tier FROM settings WHERE key = 'mail.password'")"
[ "$tier" = "secret" ] && ok "the row carries the catalogue's secret tier" \
  || bad "the row carries the catalogue's secret tier" "tier: ${tier:-missing}"

audited="$(psql_db -c "SELECT new_value FROM settings_audit WHERE key = 'mail.password' ORDER BY id DESC LIMIT 1")"
[ "$audited" = "<redacted>" ] && ok "the audit row records the change without the value" \
  || bad "the audit row records the change without the value" "audit holds: ${audited:-nothing}"

health="$(curl -fsS --max-time 20 "http://127.0.0.1:$listen_port/healthz" 2>&1)"
metrics="$(curl -fsS --max-time 20 "http://127.0.0.1:$listen_port/metrics" 2>&1)"
case "$health$metrics" in
  *hunter2*) bad "no route returns the secret" "a route body carries it" ;;
  *) ok "no route returns the secret" ;;
esac

before="$(psql_db -c "SELECT count(*) FROM settings")"
status="$(curl -s -o /dev/null -w '%{http_code}' --max-time 20 \
  -X POST "http://127.0.0.1:$listen_port/settings" --data "site.registration_open=true")"
[ "$status" = "403" ] && ok "a request with no session is refused" \
  || bad "a request with no session is refused" "HTTP $status"
count="$(psql_db -c "SELECT count(*) FROM settings")"
[ "$count" = "$before" ] && ok "the request with no session wrote no row" \
  || bad "the request with no session wrote no row" "$before -> $count row(s)"

status="$(curl -s -o /dev/null -w '%{http_code}' --max-time 20 \
  -X POST "http://127.0.0.1:$listen_port/settings" --data "site.registration_open=true" \
  -H "Cookie: cms_session=$reader_session")"
[ "$status" = "403" ] && ok "a live session without settings:write is refused" \
  || bad "a live session without settings:write is refused" "HTTP $status"
count="$(psql_db -c "SELECT count(*) FROM settings")"
[ "$count" = "$before" ] && ok "the request without the capability wrote no row" \
  || bad "the request without the capability wrote no row" "$before -> $count row(s)"

reply="$(curl -s --max-time 20 -w '\n%{http_code}' \
  -X POST "http://127.0.0.1:$listen_port/settings" \
  --data-urlencode "site.registration_domain_allowlist=half.example" \
  --data-urlencode "mail.port=not-a-port" \
  -H "Cookie: cms_session=$writer_session")"
status="$(printf '%s' "$reply" | tail -1)"
body="$(printf '%s' "$reply" | head -n -1)"

[ "$status" = "400" ] && ok "a value that is wrong for its kind is refused" \
  || bad "a value that is wrong for its kind is refused" "HTTP $status: $body"

case "$body" in
  *mail.port*) ok "the refusal names the field apply_settings refused" ;;
  *) bad "the refusal names the field apply_settings refused" "got: ${body:-nothing}" ;;
esac

allowlist="$(psql_db -c "SELECT count(*) FROM settings WHERE key = 'site.registration_domain_allowlist'")"
port_row="$(psql_db -c "SELECT count(*) FROM settings WHERE key = 'mail.port'")"
[ "$allowlist" = "0" ] && [ "$port_row" = "0" ] \
  && ok "the refused form wrote none of its fields" \
  || bad "the refused form wrote none of its fields" "allowlist $allowlist row(s), port $port_row row(s)"

printf '\n%s assertion(s) behaved, %s did not\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
