#!/usr/bin/env bash
# Drives the administrator panel through the real listener: an administrator signs in and reaches `/admin`,
# the form token gates every POST, each action writes what it says it wrote, and the audit row names the
# administrator the session belongs to. A refusal is checked against the row it must not have created.
#
# It skips (exit 77) when the PostgreSQL server binaries or the schema/tools it applies are not present.
set -uo pipefail

binary="${1:?usage: admin_panel_e2e.sh <path to cms_api>}"
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

for tool in initdb pg_ctl psql curl; do
  command -v "$tool" >/dev/null 2>&1 || { echo "  skipped: $tool is not installed"; exit 77; }
done
if [ ! -f "$repo/deploy/roles.sql" ] || [ ! -f "$repo/tools/apply-plan.sh" ]; then
  echo "  skipped: this tree has no deploy/ or tools/ to apply a schema from"
  exit 77
fi

work="$(mktemp -d)"
port=$((58000 + (RANDOM % 300)))
listen_port=$((20000 + (RANDOM % 300)))
panel_port=$((20300 + (RANDOM % 300)))
database=cmslearn_admin_panel_probe

operator="aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeee31"
operator_session="aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeee32"
operator_role="aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeee33"
verified_learner="aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeee34"

signer_password="signer-panel-password"
learner_password="learner-panel-password"

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

initdb -D "$work/data" -U postgres -A trust >"$work/initdb.log" 2>&1 \
  || { echo "  [!!] initdb failed"; exit 1; }
pg_ctl -D "$work/data" -o "-c listen_addresses='' -c unix_socket_directories=$work -p $port" \
  -l "$work/pg.log" start >/dev/null 2>&1 || { echo "  [!!] pg_ctl failed"; exit 1; }
sleep 1
psql -X -h "$work" -p "$port" -U postgres -q -v ON_ERROR_STOP=1 -f "$repo/deploy/roles.sql" >/dev/null 2>&1
psql -X -h "$work" -p "$port" -U postgres -q -c "CREATE DATABASE $database" >/dev/null 2>&1
PGHOST="$work" PGPORT="$port" PGDATABASE="$database" PGUSER=postgres \
  bash "$repo/tools/apply-plan.sh" >"$work/apply.log" 2>&1 \
  || { echo "  [!!] apply-plan failed"; tail -5 "$work/apply.log"; exit 1; }

# One administrator holding the three learner-administering capabilities, one live session for it, and one
# learner that has verified its address and is waiting for approval.
psql_db <<SQL >/dev/null || { echo "  [!!] fixture insert failed"; exit 1; }
INSERT INTO admin_accounts (id, handle, status) VALUES ('$operator', 'panel-operator', 'active');
INSERT INTO roles (id, name) VALUES ('$operator_role', 'panel_operator');
INSERT INTO role_capabilities (role_id, capability)
  VALUES ('$operator_role', 'user:read'), ('$operator_role', 'user:write'),
         ('$operator_role', 'user:approve');
INSERT INTO admin_roles (admin_id, role_id) VALUES ('$operator', '$operator_role');
INSERT INTO sessions (id, account_kind, admin_id, expires_at)
  VALUES ('$operator_session', 'admin', '$operator', now() + interval '1 hour');
INSERT INTO learner_accounts (id, handle, email, email_verified_at, status)
  VALUES ('$verified_learner', 'verified-learner', 'verified@example.org', now(), 'pending');
SQL
cat >"$work/host.env" <<ENV
CMSLEARN_LISTEN_ADDRESS=127.0.0.1
CMSLEARN_LISTEN_PORT=$listen_port
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

CMSLEARN_HOST_FILE="$work/host.env" "$binary" >"$work/api.log" 2>&1 &
api_pid=$!
sleep 2
if ! kill -0 "$api_pid" 2>/dev/null; then
  bad "the api started" "$(tail -3 "$work/api.log")"
  printf '\n%s assertion(s) behaved, %s did not\n' "$pass" "$fail"
  exit 1
fi
ok "the api started"

get() {  # get <path> [cookie]  ; a panel path goes to the panel listener, everything else to the public one
  local cookie="${2:-}" header=()
  [ -n "$cookie" ] && header=(-H "Cookie: cms_session=$cookie")
  case "$1" in
    /admin*) send "${header[@]}" "http://127.0.0.1:$panel_port$1" ;;
    *) send "${header[@]}" "http://127.0.0.1:$listen_port$1" ;;
  esac
}

send() {  # send <method-args...> <path> ; the caller supplies the body fields
  curl -s -o "$work/body" -D "$work/headers" -w '%{http_code}' --max-time 20 "$@"
}

cookie_of() {
  grep -i '^set-cookie: cms_session=' "$work/headers" | tail -1 \
    | sed -n 's/.*cms_session=\([^;]*\).*/\1/p'
}
csrf_of() {
  grep -o 'name="csrf_token" value="[^"]*"' "$work/body" | head -1 \
    | sed -e 's/.*value="//' -e 's/"$//'
}
location_of() { grep -i '^location:' "$work/headers" | tail -1; }
count_of() { psql_db -c "SELECT count(*) FROM learner_accounts WHERE handle = '$1'"; }

sign_in() {  # sign_in <identifier> <password>
  send -X POST --data-urlencode "identifier=$1" --data-urlencode "password=$2" \
    "http://127.0.0.1:$listen_port/signin"
}

post_as() {  # post_as <cookie> <path> [field=value...]
  local cookie="$1" path="$2"
  shift 2
  local fields=()
  for field in "$@"; do fields+=(--data-urlencode "$field"); done
  send -X POST -H "Cookie: cms_session=$cookie" "${fields[@]}" \
    "http://127.0.0.1:$panel_port$path"
}

status="$(get /admin)"
location="$(location_of)"
[ "$status" = "303" ] && [[ "$location" == *"/signin"* ]] \
  && ok "the panel without a session redirects to the existing sign-in page" \
  || bad "the panel without a session redirects to the existing sign-in page" "HTTP $status, $location"

status="$(get /admin "$operator_session")"
operator_token="$(csrf_of)"
[ "$status" = "200" ] && grep -q 'panel-operator' "$work/body" \
  && grep -q 'name="csrf_token"' "$work/body" \
  && ok "the signed-in administrator reaches the panel and is greeted by handle" \
  || bad "the signed-in administrator reaches the panel and is greeted by handle" "HTTP $status"
! grep -q 'argon2' "$work/body" && ! grep -q "$learner_password" "$work/body" \
  && ok "the panel carries no password hash and no secret value" \
  || bad "the panel carries no password hash and no secret value" "the body repeats a secret"

status="$(post_as "$operator_session" /admin/create-learner \
  "handle=no-token-learner" "email=no-token@example.org")"
[ "$status" = "403" ] && [ "$(count_of no-token-learner)" = "0" ] \
  && ok "a POST with no form token is refused and creates nothing" \
  || bad "a POST with no form token is refused and creates nothing" "HTTP $status"

status="$(post_as "$operator_session" /admin/create-learner \
  "csrf_token=aaaaaaaa-bbbb-4ccc-8ddd-ffffffffffff" \
  "handle=wrong-token-learner" "email=wrong@example.org")"
[ "$status" = "403" ] && [ "$(count_of wrong-token-learner)" = "0" ] \
  && ok "a POST with a wrong form token is refused and creates nothing" \
  || bad "a POST with a wrong form token is refused and creates nothing" "HTTP $status"

status="$(post_as "$operator_session" /admin/create-learner "csrf_token=$operator_token" \
  "handle=panel-learner" "email=panel-learner@example.org" "level_id=" "password=$learner_password")"
location="$(location_of)"
learner_status="$(psql_db -c "SELECT status FROM learner_accounts WHERE handle = 'panel-learner'")"
[ "$status" = "303" ] && [[ "$location" == *"/admin?done=learner-created"* ]] \
  && [ "$(count_of panel-learner)" = "1" ] && [ "$learner_status" = "active" ] \
  && ok "the create-learner form creates an active learner and answers 303" \
  || bad "the create-learner form creates an active learner and answers 303" \
      "HTTP $status, $location, ${learner_status:-missing}"

learner_id="$(psql_db -c "SELECT id FROM learner_accounts WHERE handle = 'panel-learner'")"
status="$(get "/admin?lang=en&done=learner-created" "$operator_session")"
[ "$status" = "200" ] && grep -q 'Created the learner account.' "$work/body" \
  && grep -q 'panel-learner' "$work/body" \
  && ok "the result of an action is shown on the home page" \
  || bad "the result of an action is shown on the home page" "HTTP $status"

status="$(post_as "$operator_session" /admin/create-admin "csrf_token=$operator_token" \
  "handle=panel-signer" "roles=$operator_role" "password=$signer_password")"
signer_id="$(psql_db -c "SELECT id FROM admin_accounts WHERE handle = 'panel-signer'")"
signer_roles="$(psql_db -c "SELECT count(*) FROM admin_roles WHERE admin_id = '$signer_id'")"
[ "$status" = "303" ] && [ -n "$signer_id" ] && [ "$signer_roles" = "1" ] \
  && ok "the create-admin form creates an administrator holding the actor's role" \
  || bad "the create-admin form creates an administrator holding the actor's role" "HTTP $status"

status="$(sign_in panel-signer "$signer_password")"
signer_session="$(cookie_of)"
[ "$status" = "303" ] && [ -n "$signer_session" ] \
  && ok "the created administrator signs in through the existing sign-in route" \
  || bad "the created administrator signs in through the existing sign-in route" "HTTP $status"
status="$(get /admin "$signer_session")"
signer_token="$(csrf_of)"
[ "$status" = "200" ] && grep -q 'panel-signer' "$work/body" \
  && ok "the signed-in administrator reaches the panel after signing in" \
  || bad "the signed-in administrator reaches the panel after signing in" "HTTP $status"
status="$(post_as "$signer_session" /admin/create-learner "csrf_token=$operator_token" \
  "handle=foreign-token-learner" "email=foreign@example.org")"
[ "$status" = "403" ] && [ "$(count_of foreign-token-learner)" = "0" ] \
  && ok "a token issued to another session is refused and creates nothing" \
  || bad "a token issued to another session is refused and creates nothing" "HTTP $status"
status="$(post_as "$signer_session" /admin/approve \
  "csrf_token=$signer_token" "learner_id=$verified_learner")"
approved_status="$(psql_db -c "SELECT status FROM learner_accounts WHERE id = '$verified_learner'")"
audit_actor="$(psql_db -c "SELECT actor_kind || ':' || admin_actor_id FROM audit_events
                           WHERE action = 'account.approve' ORDER BY id DESC LIMIT 1")"
[ "$status" = "303" ] && [ "$approved_status" = "active" ] && [ "$audit_actor" = "admin:$signer_id" ] \
  && ok "approving a learner moves the row and the audit row names the signing-in administrator" \
  || bad "approving a learner moves the row and the audit row names the signing-in administrator" \
      "HTTP $status, $approved_status, ${audit_actor:-no audit row}"
status="$(post_as "$signer_session" /admin/issue-verification \
  "csrf_token=$signer_token" "learner_id=$learner_id")"
tokens="$(psql_db -c "SELECT count(*) FROM learner_tokens WHERE learner_id = '$learner_id'")"
[ "$status" = "303" ] && [ "$tokens" -ge 1 ] \
  && ok "the issue-verification form issues a token for the learner" \
  || bad "the issue-verification form issues a token for the learner" "HTTP $status"
status="$(post_as "$signer_session" /admin/issue-verification \
  "csrf_token=$signer_token" "learner_id=<b>probe</b>")"
grep -q '&lt;b&gt;probe&lt;/b&gt;' "$work/body" && ! grep -q '<b>probe</b>' "$work/body" \
  && ok "a value a form supplied reaches the page escaped" \
  || bad "a value a form supplied reaches the page escaped" "HTTP $status"
status="$(post_as "$signer_session" /admin/revoke-admin \
  "csrf_token=$signer_token" "admin_id=$signer_id")"
signer_state="$(psql_db -c "SELECT status FROM admin_accounts WHERE id = '$signer_id'")"
[ "$status" = "403" ] && grep -q 'cannot revoke itself' "$work/body" \
  && [ "$signer_state" = "active" ] \
  && ok "self-revocation is refused by the action layer and its message is shown" \
  || bad "self-revocation is refused by the action layer and its message is shown" \
      "HTTP $status, $signer_state"
status="$(sign_in panel-learner@example.org "$learner_password")"
learner_session="$(cookie_of)"
[ "$status" = "303" ] && [ -n "$learner_session" ] \
  && ok "the created learner signs in through the existing sign-in route" \
  || bad "the created learner signs in through the existing sign-in route" "HTTP $status"
status="$(get /admin "$learner_session")"
[ "$status" = "403" ] && ! grep -q 'create-learner' "$work/body" \
  && ok "a learner session at the panel is refused, not served and not redirected" \
  || bad "a learner session at the panel is refused, not served and not redirected" "HTTP $status"
status="$(post_as "$learner_session" /admin/create-learner \
  "csrf_token=$signer_token" "handle=learner-token-learner")"
[ "$status" = "403" ] && [ "$(count_of learner-token-learner)" = "0" ] \
  && ok "a learner session cannot post a panel action" \
  || bad "a learner session cannot post a panel action" "HTTP $status"
status="$(sign_in panel-signer not-the-password)"
[ "$status" = "403" ] && grep -q 'the email or password is wrong' "$work/body" \
  && ok "sign-in still gives the one existing refusal message" \
  || bad "sign-in still gives the one existing refusal message" "HTTP $status"
! grep -q "$signer_password" "$work/api.log" && ! grep -q "$learner_password" "$work/api.log" \
  && ok "no password reaches the service log" \
  || bad "no password reaches the service log" "a password is in the log"
printf '\n%s assertion(s) behaved, %s did not\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
