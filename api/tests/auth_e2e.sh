#!/usr/bin/env bash
# Exercises the account surfaces through the real listener: sign-up writes a pending account with a hashed
# password, verification moves it to the state the policy declares, sign-in opens a session cookie, the
# settings page renders for a session holding `settings:read` only, and sign-out ends that session.
#
# It skips (exit 77) when the PostgreSQL server binaries or the schema/tools it applies are not present.
set -uo pipefail

binary="${1:?usage: auth_e2e.sh <path to cms_api>}"
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

for tool in initdb pg_ctl psql curl sha256sum; do
  command -v "$tool" >/dev/null 2>&1 || { echo "  skipped: $tool is not installed"; exit 77; }
done
if [ ! -f "$repo/deploy/roles.sql" ] || [ ! -f "$repo/tools/apply-plan.sh" ]; then
  echo "  skipped: this tree has no deploy/ or tools/ to apply a schema from"
  exit 77
fi

work="$(mktemp -d)"
port=$((56000 + (RANDOM % 400)))
listen_port=$((18600 + (RANDOM % 400)))
database=cmslearn_auth_probe
alice_password="correct-horse-battery-staple"
bob_password="another-long-probe-password"
token="probe-verification-token"
role="aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeee21"

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

psql_db() { psql -X -h "$work" -p "$port" -U postgres -d "$database" -tAq -v ON_ERROR_STOP=1 "$@"; }
as_actor() {
  psql_db -c "BEGIN" -c "SET LOCAL cms.actor_id = '$actor_id'" -c "$1" -c "COMMIT"
}

initdb -D "$work/data" -U postgres -A trust >"$work/initdb.log" 2>&1 || { echo "  [!!] initdb failed"; exit 1; }
pg_ctl -D "$work/data" -o "-c listen_addresses='' -c unix_socket_directories=$work -p $port" \
  -l "$work/pg.log" start >/dev/null 2>&1 || { echo "  [!!] pg_ctl failed"; exit 1; }
sleep 1
psql -X -h "$work" -p "$port" -U postgres -q -v ON_ERROR_STOP=1 -f "$repo/deploy/roles.sql" >/dev/null 2>&1
psql -X -h "$work" -p "$port" -U postgres -q -c "CREATE DATABASE $database" >/dev/null 2>&1
PGHOST="$work" PGPORT="$port" PGDATABASE="$database" PGUSER=postgres \
  bash "$repo/tools/apply-plan.sh" >"$work/apply.log" 2>&1 \
  || { echo "  [!!] apply-plan failed"; tail -5 "$work/apply.log"; exit 1; }

psql_db -c "INSERT INTO principals (kind, handle, status) VALUES ('system', 'auth-probe-actor', 'active')" \
  || { echo "  [!!] actor insert failed"; exit 1; }
actor_id="$(psql_db -c "SELECT id FROM principals WHERE handle = 'auth-probe-actor'")"
as_actor "INSERT INTO settings (key, kind, value_text, updated_by, description)
          VALUES ('site.registration_open', 'bool', 'true', '$actor_id', 'probe'),
                 ('site.registration_requires_approval', 'bool', 'true', '$actor_id', 'probe'),
                 ('site.name', 'text', 'CMS Learn', '$actor_id', 'probe'),
                 ('mail.password', 'text', 'top-secret-probe', '$actor_id', 'probe');" \
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

CMSLEARN_HOST_FILE="$work/host.env" "$binary" >"$work/api.log" 2>&1 &
api_pid=$!
sleep 2
if ! kill -0 "$api_pid" 2>/dev/null; then
  bad "the api started" "$(tail -3 "$work/api.log")"
  printf '\n%s assertion(s) behaved, %s did not\n' "$pass" "$fail"
  exit 1
fi
ok "the api started"

get() {  # get <path> [cookie]
  local cookie="${2:-}"
  if [ -n "$cookie" ]; then
    curl -s -o "$work/body" -D "$work/headers" -w '%{http_code}' --max-time 5 \
      -H "Cookie: cms_session=$cookie" "http://127.0.0.1:$listen_port$1"
  else
    curl -s -o "$work/body" -D "$work/headers" -w '%{http_code}' --max-time 5 \
      "http://127.0.0.1:$listen_port$1"
  fi
}

post() {  # post <path> <form...> [extra curl args...]
  local path="$1"
  shift
  curl -s -o "$work/body" -D "$work/headers" -w '%{http_code}' --max-time 5 \
    -X POST "http://127.0.0.1:$listen_port$path" "$@"
}

password_tag() { grep -o '<input[^>]*type="password"[^>]*>' "$work/body" | head -1; }

status="$(get /signin)"
if [ "$status" = "200" ] && grep -q 'type="password"' "$work/body"; then
  ok "the sign-in page renders its form"
else
  bad "the sign-in page renders its form" "HTTP $status"
fi
if [ -n "$(password_tag)" ] && ! password_tag | grep -q 'value='; then
  ok "the sign-in page carries no password value attribute"
else
  bad "the sign-in page carries no password value attribute" "password input carries a value"
fi

status="$(get /signup)"
if [ "$status" = "200" ] && grep -q 'name="handle"' "$work/body"; then
  ok "the sign-up page renders its form"
else
  bad "the sign-up page renders its form" "HTTP $status"
fi

status="$(post /signin --data-urlencode "email=nobody@example.org" \
  --data-urlencode "password=$alice_password")"
cp "$work/body" "$work/unknown-body"
unknown_status="$status"
status="$(post /signin --data-urlencode "email=alice@example.org" \
  --data-urlencode "password=wrong-but-long-enough")"
if [ "$status" = "$unknown_status" ] && cmp -s "$work/body" "$work/unknown-body"; then
  ok "an unknown email and a wrong password give the same answer"
else
  bad "an unknown email and a wrong password give the same answer" "HTTP $status vs $unknown_status"
fi

status="$(post /signup --data-urlencode "email=alice@example.org" --data-urlencode "handle=alice" \
  --data-urlencode "password=$alice_password")"
location="$(grep -i '^location:' "$work/headers" | tail -1)"
if [ "$status" = "303" ] && [[ "$location" == *"/signin?registered=1"* ]]; then
  ok "a signup is accepted and sent to the sign-in page"
else
  bad "a signup is accepted and sent to the sign-in page" "HTTP $status, $location"
fi

alice_status="$(psql_db -c "SELECT status FROM principals WHERE email = 'alice@example.org'")"
alice_hash="$(psql_db -c "SELECT password_hash FROM principals WHERE email = 'alice@example.org'")"
case "$alice_hash" in
  '$argon2'*) has_hash=1 ;;
  *) has_hash=0 ;;
esac
if [ "$alice_status" = "pending" ] && [ "$has_hash" = "1" ]; then
  ok "the new account is pending and its password is stored as a hash"
else
  bad "the new account is pending and its password is stored as a hash" \
      "status=$alice_status hash=${alice_hash:0:12}"
fi
if grep -q "$alice_password" "$work/api.log" \
  || [ "$(psql_db -c "SELECT count(*) FROM audit_events WHERE detail::text LIKE '%$alice_password%'")" != "0" ]; then
  bad "the password reaches neither the log nor an audit row" "the password is recorded"
else
  ok "the password reaches neither the log nor an audit row"
fi

status="$(post /signin --data-urlencode "email=alice@example.org" \
  --data-urlencode "password=$alice_password")"
if [ "$status" = "403" ] && grep -q "pending" "$work/body"; then
  ok "sign-in for an unapproved account is refused with the state it is in"
else
  bad "sign-in for an unapproved account is refused with the state it is in" "HTTP $status"
fi

token_hash="$(printf '%s' "$token" | sha256sum | cut -d' ' -f1)"
psql_db -c "INSERT INTO principal_tokens (principal_id, purpose, token_hash, expires_at)
            SELECT id, 'verify_email', '$token_hash', now() + interval '1 hour'
            FROM principals WHERE email = 'alice@example.org'" >/dev/null 2>&1 \
  || { echo "  [!!] verification token insert failed"; exit 1; }
status="$(get "/verify?token=$token")"
location="$(grep -i '^location:' "$work/headers" | tail -1)"
verified_at="$(psql_db -c "SELECT email_verified_at IS NOT NULL FROM principals WHERE email = 'alice@example.org'")"
if [ "$status" = "303" ] && [[ "$location" == *"/signin?verified=pending"* ]] \
  && [ "$verified_at" = "t" ]; then
  ok "the verification link consumes its token and reports the approval that is still needed"
else
  bad "the verification link consumes its token and reports the approval that is still needed" \
      "HTTP $status, $location, verified=$verified_at"
fi

status="$(post /signin --data-urlencode "email=alice@example.org" \
  --data-urlencode "password=$alice_password")"
if [ "$status" = "403" ] && grep -q "pending" "$work/body"; then
  ok "sign-in stays refused while the account is still pending approval"
else
  bad "sign-in stays refused while the account is still pending approval" "HTTP $status"
fi

psql_db -c "INSERT INTO roles (id, name) VALUES ('$role', 'settings_reader')" >/dev/null 2>&1
psql_db -c "INSERT INTO role_capabilities (role_id, capability) VALUES ('$role', 'settings:read')" >/dev/null 2>&1
psql_db <<SQL >/dev/null 2>&1
UPDATE principals SET status = 'active' WHERE email = 'alice@example.org';
INSERT INTO principal_roles (principal_id, role_id)
SELECT id, '$role' FROM principals WHERE email = 'alice@example.org';
SQL

headers_status="$(post /signin --data-urlencode "email=alice@example.org" \
  --data-urlencode "password=$alice_password")"
alice_cookie="$(grep -i '^set-cookie: cms_session=' "$work/headers" | tail -1 \
  | sed -n 's/.*cms_session=\([^;]*\).*/\1/p')"
set_cookie="$(grep -i '^set-cookie: cms_session=' "$work/headers" | tail -1)"
if [ "$headers_status" = "303" ] && [ -n "$alice_cookie" ] \
  && printf '%s' "$set_cookie" | grep -qi 'HttpOnly' \
  && printf '%s' "$set_cookie" | grep -qi 'SameSite=Lax' \
  && ! printf '%s' "$set_cookie" | grep -qi '; Secure'; then
  ok "sign-in opens an HttpOnly, SameSite=Lax cookie that is not Secure over plain HTTP"
else
  bad "sign-in opens an HttpOnly, SameSite=Lax cookie that is not Secure over plain HTTP" \
      "$set_cookie"
fi

post /signin --data-urlencode "email=alice@example.org" \
  --data-urlencode "password=$alice_password" -H "X-Forwarded-Proto: https" >/dev/null
if grep -i '^set-cookie: cms_session=' "$work/headers" | grep -qi '; Secure'; then
  ok "the session cookie is Secure when the request arrived through HTTPS"
else
  bad "the session cookie is Secure when the request arrived through HTTPS" "no Secure flag"
fi

status="$(get /settings "$alice_cookie")"
if [ "$status" = "200" ] && grep -q 'name="site.name"' "$work/body" \
  && grep -q 'ตั้งค่า' "$work/body"; then
  ok "the settings page renders for a session holding settings:read"
else
  bad "the settings page renders for a session holding settings:read" "HTTP $status"
fi

status="$(get "/settings?group=mail" "$alice_cookie")"
if [ "$status" = "200" ] && [ -n "$(password_tag)" ] && ! password_tag | grep -q 'value=' \
  && ! grep -q 'top-secret-probe' "$work/body" \
  && ! grep -q '<redacted>' "$work/body"; then
  ok "the settings page renders a secret as an empty password field"
else
  bad "the settings page renders a secret as an empty password field" "HTTP $status"
fi

status="$(get /settings)"
location="$(grep -i '^location:' "$work/headers" | tail -1)"
if [ "$status" = "303" ] && [[ "$location" == *"/signin"* ]]; then
  ok "the settings page without a session redirects to sign-in"
else
  bad "the settings page without a session redirects to sign-in" "HTTP $status, $location"
fi

post /signup --data-urlencode "email=bob@example.org" --data-urlencode "handle=bob" \
  --data-urlencode "password=$bob_password" >/dev/null
psql_db -c "UPDATE principals SET status = 'active', email_verified_at = now()
            WHERE email = 'bob@example.org'" >/dev/null 2>&1
post /signin --data-urlencode "email=bob@example.org" --data-urlencode "password=$bob_password" >/dev/null
bob_cookie="$(grep -i '^set-cookie: cms_session=' "$work/headers" | tail -1 \
  | sed -n 's/.*cms_session=\([^;]*\).*/\1/p')"
status="$(get /settings "$bob_cookie")"
[ "$status" = "403" ] && ok "the settings page is forbidden without settings:read" \
  || bad "the settings page is forbidden without settings:read" "HTTP $status"

status="$(post /signout -H "Cookie: cms_session=$alice_cookie")"
location="$(grep -i '^location:' "$work/headers" | tail -1)"
if [ "$status" = "303" ] && [[ "$location" == *"/"* ]] \
  && grep -i '^set-cookie: cms_session=' "$work/headers" | grep -qi 'Max-Age=0'; then
  ok "sign-out clears the session cookie"
else
  bad "sign-out clears the session cookie" "HTTP $status, $location"
fi

status="$(get /settings "$alice_cookie")"
location="$(grep -i '^location:' "$work/headers" | tail -1)"
if [ "$status" = "303" ] && [[ "$location" == *"/signin"* ]]; then
  ok "the signed-out session cannot reach the settings page"
else
  bad "the signed-out session cannot reach the settings page" "HTTP $status, $location"
fi

status="$(get /signout)"
[ "$status" = "405" ] && ok "sign-out is not reachable by GET" \
  || bad "sign-out is not reachable by GET" "HTTP $status"

status="$(get "/verify?token=not-a-token")"
[ "$status" = "400" ] && ok "an unknown verification token is refused" \
  || bad "an unknown verification token is refused" "HTTP $status"

printf '\n%s assertion(s) behaved, %s did not\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
