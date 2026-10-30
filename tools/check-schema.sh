#!/usr/bin/env bash
# Schema gate.
#
# Two claims, both about the committed pair of files in deploy/plan:
#   1. they are exactly what deploy/schema/main.sql renders today
#   2. applying them produces that schema, with nothing left to change
#
# The comparison of a render is field by field rather than byte by byte, because every render stamps
# created_at. The apply runs through tools/apply-plan.sh, which is the same entry point production
# uses, so the gate exercises the path that matters rather than a private copy of it.
#
# Needs PostgreSQL server binaries (initdb, pg_ctl) matching the version in release.toml.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
plan_dir="$repo/deploy/plan"
work="$(mktemp -d)"
port=55432

trap 'pg_ctl -D "$work/data" -m immediate stop >/dev/null 2>&1 || true; rm -rf "$work"' EXIT

die() { printf 'schema gate FAILED: %s\n' "$*" >&2; exit 1; }
note() { printf '  %s\n' "$*"; }

for tool in pgschema initdb pg_ctl createdb psql sha256sum bun; do
  command -v "$tool" >/dev/null 2>&1 || die "$tool is not installed"
done

for file in 0001_init.sql 0001_init.json CHECKSUMS; do
  [ -f "$plan_dir/$file" ] || die "deploy/plan/$file is missing; render it from deploy/schema/main.sql"
done

( cd "$repo" && sha256sum --quiet --check deploy/plan/CHECKSUMS ) \
  || die "deploy/plan does not match deploy/plan/CHECKSUMS"

# The cluster listens on a unix socket inside $work only, so it cannot collide with a local server.
initdb -D "$work/data" -U postgres -A trust >"$work/initdb.log" 2>&1 \
  || die "initdb failed; see $work/initdb.log"
pg_ctl -D "$work/data" \
  -o "-c listen_addresses='' -c unix_socket_directories=$work -p $port" \
  -l "$work/server.log" -w start >/dev/null 2>&1 \
  || die "the scratch cluster did not start; see $work/server.log"

psql -h "$work" -p "$port" -U postgres -q -v ON_ERROR_STOP=1 -f "$repo/deploy/roles.sql" >/dev/null \
  || die "deploy/roles.sql failed"

for db in render_target render_plan verify_target verify_plan; do
  createdb -h "$work" -p "$port" -U postgres "$db" || die "could not create $db"
done

# 1. The committed plan is what the source renders.
pgschema plan \
  --host "$work" --port "$port" --db render_target --user postgres \
  --plan-host "$work" --plan-port "$port" --plan-db render_plan --plan-user postgres \
  --file "$repo/deploy/schema/main.sql" \
  --output-sql "$work/render.sql" --output-json "$work/render.json" --no-color \
  >"$work/render.log" 2>&1 \
  || die "pgschema could not render deploy/schema/main.sql; see $work/render.log"

cmp -s "$work/render.sql" "$plan_dir/0001_init.sql" \
  || die "deploy/plan/0001_init.sql is not what deploy/schema/main.sql renders"
bun run "$repo/tools/compare-plan.ts" "$work/render.json" "$plan_dir/0001_init.json" \
  || die "deploy/plan/0001_init.json is not what deploy/schema/main.sql renders"

# 2. That plan creates exactly the declared schema.
PGHOST="$work" PGPORT="$port" PGDATABASE=verify_target PGUSER=postgres \
  bash "$repo/tools/apply-plan.sh" >"$work/apply.log" 2>&1 \
  || die "tools/apply-plan.sh failed; see $work/apply.log"

pgschema plan \
  --host "$work" --port "$port" --db verify_target --user postgres \
  --plan-host "$work" --plan-port "$port" --plan-db verify_plan --plan-user postgres \
  --file "$repo/deploy/schema/main.sql" \
  --output-human stdout --no-color \
  >"$work/verify.log" 2>&1 \
  || die "pgschema could not inspect the applied schema; see $work/verify.log"

grep -q "No changes detected." "$work/verify.log" \
  || die "applying the plan does not produce the schema deploy/schema/main.sql declares"

tables="$(psql -h "$work" -p "$port" -U postgres -d verify_target -tAc \
  "select count(*) from pg_tables where schemaname = 'public'")"

note "plan matches the source render"
note "plan applies cleanly; $tables table(s) in public; no residual changes"
