#!/usr/bin/env bash
# Serves the front page and one asset through the real binary, from a scratch cluster holding the shipped
# bank, and then removes `home.html` from the template tree and requires `/` to fail.
#
# The last assertion is the point of the script: a renderer that answered with an empty document, or with a
# cached page, would pass every check above and still show a learner nothing.
#
# Skips (exit 77) when the PostgreSQL server binaries are not installed.
set -uo pipefail

binary="${1:-}"
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
[ -n "$binary" ] || binary="$repo/build/safety/api/cms_api"
seeder="${2:-$(cd "$(dirname "$binary")/.." && pwd)/bank/cms_seed_content}"

if [ ! -x "$binary" ]; then
  echo "render gate FAILED: no binary at $binary; build it with: cmake --build --preset safety" >&2
  exit 1
fi
if [ ! -x "$seeder" ]; then
  echo "render gate FAILED: no seeder at $seeder; build it with: cmake --build --preset safety" >&2
  exit 1
fi

for tool in initdb pg_ctl psql curl; do
  command -v "$tool" >/dev/null 2>&1 || { echo "  skipped: $tool is not installed"; exit 77; }
done

work="$(mktemp -d)"
port=$((57000 + (RANDOM % 400)))
listen_port=$((19000 + (RANDOM % 400)))
broken_port=$((listen_port + 400))
database=cmslearn_render_probe

cleanup() {
  [ -n "${api_pid:-}" ] && kill "$api_pid" 2>/dev/null
  [ -n "${broken_pid:-}" ] && kill "$broken_pid" 2>/dev/null
  pg_ctl -D "$work/data" -m immediate stop >/dev/null 2>&1
  rm -rf "$work"
}
trap cleanup EXIT

pass=0
fail=0
ok() { printf '  [ok]   %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  [!!]   %s — %s\n' "$1" "$2"; fail=$((fail + 1)); }

psql_db() { psql -X -h "$work" -p "$port" -U postgres -d "$database" -tAc "$1"; }

initdb -D "$work/data" -U postgres -A trust >"$work/initdb.log" 2>&1 || { echo "  [!!] initdb failed"; exit 1; }
pg_ctl -D "$work/data" -o "-c listen_addresses='' -c unix_socket_directories=$work -p $port" \
  -l "$work/pg.log" start >/dev/null 2>&1 || { echo "  [!!] pg_ctl failed"; exit 1; }
sleep 1
psql -X -h "$work" -p "$port" -U postgres -q -v ON_ERROR_STOP=1 -f "$repo/deploy/roles.sql" >/dev/null 2>&1
psql -X -h "$work" -p "$port" -U postgres -q -c "CREATE DATABASE $database" >/dev/null 2>&1
PGHOST="$work" PGPORT="$port" PGDATABASE="$database" PGUSER=postgres \
  bash "$repo/tools/apply-plan.sh" >"$work/apply.log" 2>&1 \
  || { echo "  [!!] apply-plan failed"; tail -5 "$work/apply.log"; exit 1; }

export CMSLEARN_DATABASE_URL="host=$work port=$port dbname=$database user=postgres"
"$seeder" --content "$repo/content" >"$work/seed.log" 2>&1 \
  || { echo "  [!!] the seeder failed"; tail -5 "$work/seed.log"; exit 1; }

stage_title="$(psql_db 'SELECT title_th FROM track_stages ORDER BY track_id, position LIMIT 1')"
[ -n "$stage_title" ] || { echo "  [!!] the seeded bank has no stage to look for"; exit 1; }

# The templates are copied so the falsification below removes a page from a tree the service is serving,
# not from the checkout.
cp -R "$repo/api/templates" "$work/templates"

write_host_file() {
  cat >"$1" <<ENV
CMSLEARN_LISTEN_ADDRESS=127.0.0.1
CMSLEARN_LISTEN_PORT=$2
CMSLEARN_DATABASE_URL=$CMSLEARN_DATABASE_URL
CMSLEARN_SEAL_KEY=0123456789abcdef0123456789abcdef
CMSLEARN_BOOTSTRAP_ADMIN=operator@example.org
CMSLEARN_TLS_MODE=proxy
CMSLEARN_ADMIN_SOCKET=$3
CMSLEARN_ADMIN_UID=$(id -u)
CMSLEARN_TEMPLATE_ROOT=$work/templates
CMSLEARN_STATIC_ROOT=$repo/web/public
ENV
}

write_host_file "$work/host.env" "$listen_port" "$work/admin.sock"
CMSLEARN_HOST_FILE="$work/host.env" "$binary" >"$work/api.log" 2>&1 &
api_pid=$!
sleep 2

if ! kill -0 "$api_pid" 2>/dev/null; then
  bad "the api started" "$(tail -3 "$work/api.log")"
  printf '\n%s assertion(s) behaved, %s did not\n' "$pass" "$fail"
  exit 1
fi

status="$(curl -s -o "$work/home.out" -w '%{http_code}' --max-time 5 "http://127.0.0.1:$listen_port/")"
[ "$status" = "200" ] && ok "/ is 200" || bad "/ is 200" "HTTP $status: $(head -c 200 "$work/home.out")"

grep -qF -- "$stage_title" "$work/home.out" \
  && ok "/ shows the seeded stage named $stage_title" \
  || bad "/ shows the seeded stage" "$stage_title is not in the page"

status="$(curl -s -o "$work/app.out" -D "$work/app.headers" -w '%{http_code}' --max-time 5 \
  "http://127.0.0.1:$listen_port/static/app.css")"
[ "$status" = "200" ] && ok "/static/app.css is 200" || bad "/static/app.css is 200" "HTTP $status"

grep -qi '^content-type:.*text/css' "$work/app.headers" \
  && ok "/static/app.css is served as text/css" \
  || bad "/static/app.css is served as text/css" "$(grep -i '^content-type:' "$work/app.headers")"

cmp -s "$work/app.out" "$repo/web/public/app.css" \
  && ok "/static/app.css is the published file" \
  || bad "/static/app.css is the published file" "the bytes differ"

# --path-as-is so the shell and curl do not resolve the traversal before it is sent.
status="$(curl -s --path-as-is -o "$work/escape.out" -w '%{http_code}' --max-time 5 \
  "http://127.0.0.1:$listen_port/static/../api/src/main.cpp")"
if [ "$status" != "200" ] && ! grep -qF 'CMSLEARN_HOST_FILE is not set' "$work/escape.out"; then
  ok "/static/../api/src/main.cpp returns no source file (HTTP $status)"
else
  bad "/static/../api/src/main.cpp returns no source file" "HTTP $status: $(head -c 200 "$work/escape.out")"
fi

kill "$api_pid" 2>/dev/null
wait "$api_pid" 2>/dev/null
api_pid=""

# The falsification: the same tree, the same cluster, one page removed.
rm -f "$work/templates/home.html"
write_host_file "$work/host-broken.env" "$broken_port" "$work/admin-broken.sock"
CMSLEARN_HOST_FILE="$work/host-broken.env" "$binary" >"$work/api-broken.log" 2>&1 &
broken_pid=$!
sleep 2

if ! kill -0 "$broken_pid" 2>/dev/null; then
  bad "the api started without home.html" "$(tail -3 "$work/api-broken.log")"
else
  status="$(curl -s -o "$work/broken.out" -w '%{http_code}' --max-time 5 \
    "http://127.0.0.1:$broken_port/")"
  case "$status" in
    5*) ok "/ is a server error when home.html is gone (HTTP $status)" ;;
    *) bad "/ is a server error when home.html is gone" "HTTP $status" ;;
  esac
fi

printf '\n%s assertion(s) behaved, %s did not\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
