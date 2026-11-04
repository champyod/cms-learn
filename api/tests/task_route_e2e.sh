#!/usr/bin/env bash
# Serves the task page and the fragment it polls through the real route against a scratch cluster.
#
# What one commit's contract says: no session is a redirect to sign-in (not a 403 and not a page), an account
# that is not approved is treated the same way, a task with no visibility row is visible only to a session
# holding content:read, the page renders the published version, an unknown/draft/hidden task answers one 404
# body, a server error names neither the principal nor the task, and /tasks/{id}/verdict exists.
#
# The audience values come from cms_principal_value, the one module allowed to compute them.
set -uo pipefail

binary="${1:?usage: task_route_e2e.sh <path to cms_api>}"
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
principal_value="${2:-$(cd "$(dirname "$binary")/.." && pwd)/bank/cms_principal_value}"

for tool in initdb pg_ctl psql curl; do
  command -v "$tool" >/dev/null 2>&1 || { echo "  skipped: $tool is not installed"; exit 77; }
done
if [ ! -x "$principal_value" ]; then
  echo "  skipped: this tree has no cms_principal_value at $principal_value"
  exit 77
fi
if [ ! -f "$repo/deploy/roles.sql" ] || [ ! -f "$repo/tools/apply-plan.sh" ]; then
  echo "  skipped: this tree has no deploy/ or tools/ to apply a schema from"
  exit 77
fi

work="$(mktemp -d)"
port=$((57000 + (RANDOM % 400)))
listen_port=$((19000 + (RANDOM % 400)))
database=cmslearn_task_route_probe
learner="11111111-aaaa-4ccc-8ddd-eeeeeeeeee01"
learner_session="11111111-aaaa-4ccc-8ddd-eeeeeeeeee02"
reader="22222222-aaaa-4ccc-8ddd-eeeeeeeeee01"
reader_session="22222222-aaaa-4ccc-8ddd-eeeeeeeeee02"
pending="33333333-aaaa-4ccc-8ddd-eeeeeeeeee01"
pending_session="33333333-aaaa-4ccc-8ddd-eeeeeeeeee02"
role="33333333-aaaa-4ccc-8ddd-eeeeeeeeee03"
other="44444444-aaaa-4ccc-8ddd-eeeeeeeeee01"
other_session="44444444-aaaa-4ccc-8ddd-eeeeeeeeee02"
solution="REFERENCE_SOLUTION_1907"
server_error="the request could not be served"

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

psql_db <<SQL >/dev/null 2>&1
INSERT INTO principals (id, kind, handle, status)
VALUES ('$learner', 'user', 'route-learner', 'active'),
       ('$reader', 'user', 'route-reader', 'active'),
       ('$pending', 'user', 'route-pending', 'pending'),
       ('$other', 'user', 'route-other', 'active');
INSERT INTO sessions (id, principal_id, expires_at)
VALUES ('$learner_session', '$learner', now() + interval '1 hour'),
       ('$reader_session', '$reader', now() + interval '1 hour'),
       ('$pending_session', '$pending', now() + interval '1 hour'),
       ('$other_session', '$other', now() + interval '1 hour');
INSERT INTO roles (id, name) VALUES ('$role', 'route_content_reader');
INSERT INTO role_capabilities (role_id, capability) VALUES ('$role', 'content:read');
INSERT INTO principal_roles (principal_id, role_id) VALUES ('$reader', '$role');
SQL

everyone="$("$principal_value" everyone)"
learner_value="$("$principal_value" encode learner "$(psql_db -c "SELECT serial_id FROM principals WHERE id = '$learner'")")"

psql_db <<SQL >/dev/null 2>&1
INSERT INTO tasks (id, version, status, title_en, title_th, difficulty, time_limit_ms,
                   memory_limit_mb, checker, origin, verification, solution, content_hash)
VALUES ('everyone-task', 1, 'published', 'Everyone Task', 'งานสาธารณะ', 900, 1000, 256, 'token',
        'original', 'verified', '$solution', repeat('a', 64)),
       ('learner-task', 1, 'published', 'Learner Task', 'งานเฉพาะผู้เรียน', 900, 1000, 256, 'token',
        'original', 'verified', '$solution', repeat('b', 64)),
       ('draft-task', 1, 'draft', 'Draft Task', 'งานฉบับร่าง', 900, 1000, 256, 'token',
        'original', 'pending', '$solution', repeat('c', 64)),
       ('hidden-task', 1, 'published', 'Hidden Task', 'งานที่ซ่อนไว้', 900, 1000, 256, 'token',
        'original', 'verified', '$solution', repeat('d', 64)),
       ('broken-task', 1, 'published', 'Broken Task', 'งานที่ไม่มีคำบรรยาย', 900, 1000, 256, 'token',
        'original', 'verified', '$solution', repeat('e', 64));

INSERT INTO task_visibility (task_id, principal_id)
VALUES ('everyone-task', $everyone),
       ('learner-task', $learner_value),
       ('draft-task', $everyone),
       ('broken-task', $everyone);

INSERT INTO task_statements (task_id, task_version, locale, body)
VALUES ('everyone-task', 1, 'en', E'# Everyone Task\n\nAnswer each query in order.'),
       ('everyone-task', 1, 'th', E'# งานสาธารณะ\n\nจงตอบผลรวมของแต่ละช่วง'),
       ('learner-task', 1, 'en', E'# Learner Task\n\nOnly its learner sees this.'),
       ('learner-task', 1, 'th', E'# งานเฉพาะผู้เรียน\n\nเห็นเฉพาะผู้เรียนที่ได้รับสิทธิ์'),
       ('hidden-task', 1, 'en', E'# Hidden Task\n\nVisible to content readers only.'),
       ('hidden-task', 1, 'th', E'# งานที่ซ่อนไว้\n\nเห็นเฉพาะผู้ที่ถือสิทธิ์อ่านเนื้อหา');

INSERT INTO task_examples (task_id, task_version, ordinal, input, expected_output)
VALUES ('everyone-task', 1, 1, E'3 2\n1 2 3\n1 2\n2 3\n', E'3\n5\n'),
       ('learner-task', 1, 1, E'2 2\n4 5\n1 1\n2 2\n', E'9\n'),
       ('hidden-task', 1, 1, E'1 1\n7\n1 1\n', E'7\n');
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

fetch() {
  local path="$1"
  local cookie="${2:-}"
  if [ -n "$cookie" ]; then
    curl -s -o "$work/body" -D "$work/headers" -w '%{http_code}' --max-time 5 \
      -H "Cookie: cms_session=$cookie" "http://127.0.0.1:$listen_port$path"
  else
    curl -s -o "$work/body" -D "$work/headers" -w '%{http_code}' --max-time 5 \
      "http://127.0.0.1:$listen_port$path"
  fi
}
location() { grep -i '^location:' "$work/headers" | tail -1; }

status="$(fetch /tasks/everyone-task)"
if [ "$status" = "303" ] && [[ "$(location)" == *"/signin"* ]]; then
  ok "a task page without a session redirects to sign-in"
else
  bad "a task page without a session redirects to sign-in" "HTTP $status"
fi

status="$(fetch /tasks/everyone-task "$pending_session")"
if [ "$status" = "303" ] && [[ "$(location)" == *"/signin"* ]]; then
  ok "a session for an unapproved account is redirected to sign-in"
else
  bad "a session for an unapproved account is redirected to sign-in" "HTTP $status"
fi

status="$(fetch /tasks/everyone-task "$learner_session")"
if [ "$status" = "200" ] && grep -q "งานสาธารณะ" "$work/body" && grep -q "1 2 3" "$work/body"; then
  ok "a published task the viewer may see renders its title and first example"
else
  bad "a published task the viewer may see renders its title and first example" "HTTP $status"
fi
if grep -q "$solution" "$work/body"; then
  bad "the page carries no reference solution" "the solution text is on the page"
else
  ok "the page carries no reference solution"
fi
if grep -q 'spellCheck="false"></textarea>' "$work/body"; then
  ok "the starter source is empty because the content model has no starter file"
else
  bad "the starter source is empty because the content model has no starter file" "the textarea is not empty"
fi

status="$(fetch "/tasks/everyone-task?lang=en" "$learner_session")"
if [ "$status" = "200" ] && grep -q "Everyone Task" "$work/body" \
  && grep -q "Answer each query in order." "$work/body"; then
  ok "the page answers in the language the request asked for"
else
  bad "the page answers in the language the request asked for" "HTTP $status"
fi

status="$(fetch /tasks/learner-task "$learner_session")"
[ "$status" = "200" ] && ok "a task granted to the signed-in learner is served" \
  || bad "a task granted to the signed-in learner is served" "HTTP $status"

status="$(fetch /tasks/learner-task "$other_session")"
[ "$status" = "404" ] && ok "a task granted to one learner is 404 for another learner" \
  || bad "a task granted to one learner is 404 for another learner" "HTTP $status"

status="$(fetch /tasks/learner-task "$reader_session")"
[ "$status" = "200" ] && ok "content:read sees a task granted to one learner" \
  || bad "content:read sees a task granted to one learner" "HTTP $status"

status="$(fetch /tasks/learner-task)"
if [ "$status" = "303" ] && [[ "$(location)" == *"/signin"* ]]; then
  ok "a learner-granted task without a session redirects to sign-in"
else
  bad "a learner-granted task without a session redirects to sign-in" "HTTP $status"
fi

status="$(fetch /tasks/no-such-task "$learner_session")"
cp "$work/body" "$work/unknown-body"
[ "$status" = "404" ] && ok "an unknown id is 404" || bad "an unknown id is 404" "HTTP $status"

status="$(fetch /tasks/draft-task "$learner_session")"
if [ "$status" = "404" ] && cmp -s "$work/body" "$work/unknown-body"; then
  ok "a draft task is 404 with the same body as an unknown id"
else
  bad "a draft task is 404 with the same body as an unknown id" "HTTP $status"
fi

status="$(fetch /tasks/hidden-task "$learner_session")"
if [ "$status" = "404" ] && cmp -s "$work/body" "$work/unknown-body"; then
  ok "a task with no visibility row is 404 for a learner, same body as an unknown id"
else
  bad "a task with no visibility row is 404 for a learner, same body as an unknown id" "HTTP $status"
fi

status="$(fetch /tasks/hidden-task "$reader_session")"
if [ "$status" = "200" ] && grep -q "งานที่ซ่อนไว้" "$work/body"; then
  ok "a task with no visibility row is served to a session holding content:read"
else
  bad "a task with no visibility row is served to a session holding content:read" "HTTP $status"
fi

status="$(fetch /tasks/broken-task "$learner_session")"
if [ "$status" = "500" ] && grep -qx "$server_error" "$work/body" \
  && ! grep -q "broken-task" "$work/body" && ! grep -q "$learner" "$work/body"; then
  ok "a 500 body names neither the task nor the principal"
else
  bad "a 500 body names neither the task nor the principal" "HTTP $status: $(head -1 "$work/body")"
fi

status="$(fetch /tasks/no-such-task/verdict "$learner_session")"
if [ "$status" = "404" ] && cmp -s "$work/body" "$work/unknown-body"; then
  ok "the verdict endpoint answers the page's one 404 for an unknown id"
else
  bad "the verdict endpoint answers the page's one 404 for an unknown id" "HTTP $status"
fi

status="$(fetch /tasks/hidden-task/verdict "$learner_session")"
if [ "$status" = "404" ] && cmp -s "$work/body" "$work/unknown-body"; then
  ok "the verdict endpoint answers the page's one 404 for a hidden task"
else
  bad "the verdict endpoint answers the page's one 404 for a hidden task" "HTTP $status"
fi

status="$(fetch /tasks/everyone-task/verdict)"
if [ "$status" = "303" ] && [[ "$(location)" == *"/signin"* ]]; then
  ok "the verdict endpoint without a session redirects to sign-in"
else
  bad "the verdict endpoint without a session redirects to sign-in" "HTTP $status"
fi

status="$(fetch /tasks/everyone-task/verdict "$learner_session")"
if [ "$status" = "200" ] && grep -q 'hx-get="/tasks/everyone-task/verdict"' "$work/body" \
  && grep -q "ยังไม่มีผลการตรวจ" "$work/body"; then
  ok "the verdict fragment keeps polling while no verdict has arrived"
else
  bad "the verdict fragment keeps polling while no verdict has arrived" "HTTP $status"
fi

psql_db -c "INSERT INTO submissions (account_id, task_id, task_version, source, verdict, judged_at)
            VALUES ('$learner', 'everyone-task', 1, 'int main() {}', 'ok', now())" >/dev/null 2>&1 \
  || { echo "  [!!] judged submission insert failed"; exit 1; }
status="$(fetch /tasks/everyone-task/verdict "$learner_session")"
if [ "$status" = "200" ] && grep -q "ผ่าน" "$work/body" && ! grep -q "hx-get" "$work/body"; then
  ok "the verdict fragment shows the verdict and stops polling once judged"
else
  bad "the verdict fragment shows the verdict and stops polling once judged" "HTTP $status"
fi

printf '\n%s assertion(s) behaved, %s did not\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
