#!/usr/bin/env bash
# The authoring form, through the real binary: it is generated from the stored definitions (a field row added
# appears, one removed disappears, with no rebuild), it saves a draft that stays a draft, it refuses a key the
# form does not own and a value the definition forbids by name, and it refuses a request without the token or a
# session that is not an administrator's.
#
# Skips (exit 77) when the PostgreSQL server binaries or the schema are not present.
set -uo pipefail

binary="${1:?usage: task_form_e2e.sh <path to cms_api>}"
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
seeder="${2:-$(cd "$(dirname "$binary")/.." && pwd)/bank/cms_seed_content}"

for tool in initdb pg_ctl createdb psql curl; do
  command -v "$tool" >/dev/null 2>&1 || { echo "  skipped: $tool is not installed"; exit 77; }
done
[ -x "$seeder" ] || { echo "  skipped: this tree has no cms_seed_content"; exit 77; }
[ -f "$repo/tools/apply-plan.sh" ] || { echo "  skipped: this tree has no deploy/ to apply"; exit 77; }

work="$(mktemp -d)"
port=$((64500 + (RANDOM % 40)))
listen=$((25300 + (RANDOM % 90)))
panel=$((listen + 1))
cleanup() {
  [ -n "${api_pid:-}" ] && kill -9 "$api_pid" 2>/dev/null
  pg_ctl -D "$work/data" -m immediate stop >/dev/null 2>&1
  rm -rf "$work"
}
trap cleanup EXIT

pass=0
fail=0
ok() { printf '  [ok]   %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  [!!]   %s — %s\n' "$1" "$2"; fail=$((fail + 1)); }

psql_db() { psql -X -h "$work" -p "$port" -U postgres -d taskformprobe -tAq -c "$1"; }

initdb -D "$work/data" -U postgres -A trust >/dev/null 2>&1 || { echo "  [!!] initdb failed"; exit 1; }
pg_ctl -D "$work/data" -o "-c listen_addresses='' -c unix_socket_directories=$work -p $port" \
  -l "$work/pg.log" start >/dev/null 2>&1 || { echo "  [!!] pg_ctl failed"; exit 1; }
sleep 1
psql -X -h "$work" -p "$port" -U postgres -q -f "$repo/deploy/roles.sql" >/dev/null 2>&1
createdb -h "$work" -p "$port" -U postgres taskformprobe >/dev/null 2>&1
PGHOST="$work" PGPORT="$port" PGDATABASE=taskformprobe PGUSER=postgres \
  bash "$repo/tools/apply-plan.sh" >/dev/null 2>&1 || { echo "  [!!] apply-plan failed"; exit 1; }
CMSLEARN_DATABASE_URL="host=$work port=$port dbname=taskformprobe user=postgres" \
  "$seeder" --content "$repo/content" >/dev/null 2>&1 || { echo "  [!!] the seeder failed"; exit 1; }

cat > "$work/host.env" <<ENV
CMSLEARN_LISTEN_ADDRESS=127.0.0.1
CMSLEARN_LISTEN_PORT=$listen
CMSLEARN_PANEL_LISTEN_ADDRESS=127.0.0.1
CMSLEARN_PANEL_LISTEN_PORT=$panel
CMSLEARN_DATABASE_URL=host=$work port=$port dbname=taskformprobe user=postgres
CMSLEARN_SEAL_KEY=0123456789abcdef0123456789abcdef
CMSLEARN_BOOTSTRAP_ADMIN=author@example.org
CMSLEARN_TLS_MODE=proxy
CMSLEARN_ADMIN_SOCKET=$work/admin.sock
CMSLEARN_ADMIN_UID=$(id -u)
CMSLEARN_TEMPLATE_ROOT=$repo/api/templates
CMSLEARN_STATIC_ROOT=$repo/web/public
ENV
CMSLEARN_HOST_FILE="$work/host.env" "$binary" >"$work/api.log" 2>&1 &
api_pid=$!
for _ in $(seq 1 80); do
  sleep 0.25
  curl -sf -o /dev/null --max-time 2 "http://127.0.0.1:$listen/healthz" && break
done
kill -0 "$api_pid" 2>/dev/null || { echo "  [!!] the api did not start: $(tail -3 "$work/api.log")"; exit 1; }

author_id="$(psql_db "SELECT id FROM admin_accounts ORDER BY handle LIMIT 1")"
author_role="$(psql_db "SELECT id FROM roles WHERE name = 'author'")"
psql_db "INSERT INTO admin_roles (admin_id, role_id) VALUES ('$author_id', '$author_role')" >/dev/null
session=aaaaaaaa-0000-4000-8000-0000000000e1
psql_db "INSERT INTO sessions (id, account_kind, admin_id, expires_at) VALUES ('$session', 'admin', '$author_id', now() + interval '1 hour')" >/dev/null
learner=aaaaaaaa-0000-4000-8000-0000000000e2
learner_session=aaaaaaaa-0000-4000-8000-0000000000e3
psql_db "INSERT INTO learner_accounts (id, handle, status) VALUES ('$learner', 'form_learner', 'active')" >/dev/null
psql_db "INSERT INTO sessions (id, account_kind, learner_id, expires_at) VALUES ('$learner_session', 'learner', '$learner', now() + interval '1 hour')" >/dev/null

get() { curl -s -o "$work/body" -w '%{http_code}' --max-time 20 -H "Cookie: cms_session=$1" "http://127.0.0.1:$panel$2"; }
post() {
  local cookie="$1"
  shift
  local fields=()
  for field in "$@"; do fields+=(--data-urlencode "$field"); done
  curl -s -o "$work/body" -w '%{http_code}' --max-time 20 -H "Cookie: cms_session=$cookie" \
    "${fields[@]}" "http://127.0.0.1:$panel/admin/tasks/new"
}

status="$(get "$session" /admin/tasks/new)"
[ "$status" = "200" ] && grep -q 'type_id=batch' "$work/body" \
  && ok "an author opens the form and sees the types the database holds" \
  || bad "an author opens the form and sees the types the database holds" "HTTP $status"

status="$(get "$session" '/admin/tasks/new?type_id=batch')"
token="$(grep -o 'name="csrf_token" value="[^"]*"' "$work/body" | head -1 | sed 's/.*value="//;s/"//')"
[ "$status" = "200" ] && grep -q 'name="statement"' "$work/body" && grep -q 'name="score_mode"' "$work/body" \
  && ok "the form renders the fields the type defines" \
  || bad "the form renders the fields the type defines" "HTTP $status"

status="$(post "$session" "csrf_token=$token" "type_id=batch" "title_en=Probe draft" \
  "title_th=ทดสอบฉบับร่าง" "difficulty=1200" "statement=อ่านแล้วบวก" "score_mode=per_case" "case_count=5")"
row="$(psql_db "SELECT status || '/' || verification FROM tasks WHERE title_en = 'Probe draft'")"
values="$(psql_db "SELECT count(*) FROM task_field_values v JOIN tasks t ON t.id = v.task_id WHERE t.title_en = 'Probe draft'")"
[ "$status" = "303" ] && [ "$row" = "draft/pending" ] && [ "$values" = "3" ] \
  && ok "saving the form writes a draft carrying its type's fields, and no other status" \
  || bad "saving the form writes a draft carrying its type's fields, and no other status" \
      "HTTP $status, row ${row:-none}, values $values"

status="$(post "$session" "csrf_token=$token" "type_id=batch" "title_en=Probe bad" "title_th=ไม่ดี" \
  "difficulty=1200" "statement=x" "status=published")"
[ "$status" = "400" ] && grep -q 'does not carry the field status' "$work/body" \
  && [ "$(psql_db "SELECT count(*) FROM tasks WHERE title_en = 'Probe bad'")" = "0" ] \
  && ok "a request naming a column the form does not own is refused by name and writes nothing" \
  || bad "a request naming a column the form does not own is refused by name and writes nothing" "HTTP $status"

status="$(post "$session" "csrf_token=$token" "type_id=batch" "title_en=Probe bad" "title_th=ไม่ดี" \
  "difficulty=1200" "statement=x" "score_mode=not_a_choice")"
[ "$status" = "400" ] && grep -q 'score_mode' "$work/body" \
  && ok "a value the definition forbids is refused with the field's name" \
  || bad "a value the definition forbids is refused with the field's name" "HTTP $status"

status="$(post "$session" "csrf_token=$token" "type_id=batch" "title_en=Probe key file" "title_th=กุญแจ" \
  "difficulty=1200" "statement=x" "solution=tasks/probe/absent.cpp")"
[ "$status" = "400" ] && grep -q 'the field solution' "$work/body" && grep -q 'not an artefact id' "$work/body" \
  && [ "$(psql_db "SELECT count(*) FROM tasks WHERE title_en = 'Probe key file'")" = "0" ] \
  && ok "a file field holding a store key is refused by name and writes nothing" \
  || bad "a file field holding a store key is refused by name and writes nothing" "HTTP $status"

# The value a file field carries: the id of a stored artefact, which the panel resolves to the object's bytes.
digest="$(printf 'int main() {}' | sha256sum | cut -d' ' -f1)"
psql_db "INSERT INTO generation_jobs (task_id, task_version) SELECT id, version FROM tasks WHERE title_en = 'Probe draft'; INSERT INTO generation_artefacts (job_id, step, kind, object_key, digest, model_id, prompt_version, seed, call_index) SELECT max(id), 5, 'solution', 'tasks/probe/form.cpp', '$digest', 'probe-model', 'probe-v1', 1, 0 FROM generation_jobs;" >/dev/null
artefact="$(psql_db "SELECT max(id) FROM generation_artefacts")"
status="$(post "$session" "csrf_token=$token" "type_id=batch" "title_en=Probe id file" "title_th=รหัส" \
  "difficulty=1200" "statement=x" "solution=$artefact")"
held="$(psql_db "SELECT jsonb_typeof(v.value) || '/' || (v.value #>> '{}') FROM task_field_values v JOIN tasks t ON t.id = v.task_id WHERE t.title_en = 'Probe id file' AND v.field_key = 'solution'")"
[ "$status" = "303" ] && [ "$held" = "number/$artefact" ] \
  && ok "a file field saves the artefact id it was given, as a number" \
  || bad "a file field saves the artefact id it was given, as a number" "HTTP $status, held ${held:-none}"

status="$(curl -s -o /dev/null -w '%{http_code}' --max-time 20 -H "Cookie: cms_session=$session" \
  --data-urlencode "type_id=batch" "http://127.0.0.1:$panel/admin/tasks/new")"
[ "$status" = "403" ] && ok "a POST without the session token is refused" \
  || bad "a POST without the session token is refused" "HTTP $status"

[ "$(get "$learner_session" /admin/tasks/new)" = "403" ] && ok "a learner session is refused" \
  || bad "a learner session is refused" "not 403"

# The form follows the definition rows: one added appears, one removed disappears, with no rebuild.
psql_db "SELECT set_config('cms.actor_id', '$author_id', false), set_config('cms.actor_kind', 'admin', false), set_config('cms.invoked_by', 'task_form_e2e', false); INSERT INTO task_field_defs (task_type_id, key, kind, label_en, position) VALUES ('batch', 'probe_extra', 'text', 'Extra', 99);" >/dev/null
get "$session" '/admin/tasks/new?type_id=batch' >/dev/null
added="$(grep -c 'name="probe_extra"' "$work/body")"
psql_db "SELECT set_config('cms.actor_id', '$author_id', false), set_config('cms.actor_kind', 'admin', false), set_config('cms.invoked_by', 'task_form_e2e', false); DELETE FROM task_field_defs WHERE task_type_id = 'batch' AND key = 'probe_extra';" >/dev/null
get "$session" '/admin/tasks/new?type_id=batch' >/dev/null
removed="$(grep -c 'name="probe_extra"' "$work/body")"
[ "$added" = "1" ] && [ "$removed" = "0" ] \
  && ok "a field row added appears on the form and one removed disappears, with no rebuild" \
  || bad "a field row added appears on the form and one removed disappears, with no rebuild" \
      "added $added, removed $removed"

printf '\n%s assertion(s) behaved, %s did not\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
