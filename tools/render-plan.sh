#!/usr/bin/env bash
# Renders the committed plan, which only a driver may write.
#
# deploy/plan is a generated artifact compared byte for byte by check-schema.sh, so a hand-edited plan is
# indistinguishable from a rendered one until the gate runs. This is the command that produces it: the same
# pgschema invocation the gate makes, writing the SQL, the JSON and the digests together so the three can
# never name different renders.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
plan_dir="$repo/deploy/plan"

# The renderer is pinned for the same reason the verifier is: a plan is only comparable to a render made by
# the version that made it.
# shellcheck source=tools/pinned-pgschema.sh
. "$repo/tools/pinned-pgschema.sh"
# shellcheck source=tools/postgres-cluster.sh
. "$repo/tools/postgres-cluster.sh"
work="$(mktemp -d)"
port=55434

die() { printf 'render-plan: %s\n' "$*" >&2; exit 1; }
note() { printf '  %s\n' "$*"; }

cleanup() {
  pg_cluster_stop "$work" >/dev/null 2>&1 || true
  rm -rf "$work"
}
trap cleanup EXIT

pg_cluster_binaries
for tool in initdb pg_ctl createdb psql sha256sum sed; do
  command -v "$tool" >/dev/null 2>&1 || die "$tool is not installed"
done

pg_cluster_prepare "$work" || die "the scratch cluster cannot be handed to the postgres user"
pg_cluster_init "$work" >"$work/initdb.log" 2>&1 \
  || die "initdb failed; see $work/initdb.log"
pg_cluster_start "$work" "$port" "$work/server.log" >/dev/null 2>&1 \
  || die "the scratch cluster did not start; see $work/server.log"

# The plan stamps its owner (ALTER DEFAULT PRIVILEGES FOR ROLE ...), so it must be rendered as the role
# that owns the deployment's database. That role is the deployment fact in deploy/.env.example, not the
# render machine's postgres: a plan rendered as postgres fails a fresh deployment on a role it never creates.
owner="$(sed -n 's/^POSTGRES_USER=//p' "$repo/deploy/.env.example" | head -1)"
[ -n "$owner" ] || die "deploy/.env.example names no POSTGRES_USER; the plan has no owner to render for"

psql -h "$work" -p "$port" -U postgres -q -v ON_ERROR_STOP=1 \
  -c "CREATE ROLE \"$owner\" LOGIN SUPERUSER PASSWORD 'render-only'" >/dev/null \
  || die "could not create the deployment owner $owner"
psql -h "$work" -p "$port" -U postgres -q -v ON_ERROR_STOP=1 -f "$repo/deploy/roles.sql" >/dev/null \
  || die "deploy/roles.sql failed"

for db in render_target render_plan; do
  createdb -h "$work" -p "$port" -U postgres -O "$owner" "$db" || die "could not create $db"
done

"$pgschema_bin" plan \
  --host "$work" --port "$port" --db render_target --user "$owner" \
  --plan-host "$work" --plan-port "$port" --plan-db render_plan --plan-user "$owner" \
  --file "$repo/deploy/schema/main.sql" \
  --output-sql "$work/render.sql" --output-json "$work/render.json" --no-color \
  >"$work/render.log" 2>&1 \
  || die "pgschema could not render deploy/schema/main.sql; see $work/render.log"

# Each render stamps created_at; keeping the committed stamp makes a clean-tree render byte-identical.
if [ -f "$plan_dir/0001_init.json" ]; then
  stamp="$(sed -n 's/.*"created_at": "\([^"]*\)".*/\1/p' "$plan_dir/0001_init.json" | head -1)"
  if [ -n "$stamp" ]; then
    sed -i "s/\"created_at\": \"[^\"]*\"/\"created_at\": \"$stamp\"/" "$work/render.json"
  fi
fi

cp "$work/render.sql" "$plan_dir/0001_init.sql"
cp "$work/render.json" "$plan_dir/0001_init.json"
( cd "$repo" && sha256sum deploy/plan/0001_init.sql deploy/plan/0001_init.json > deploy/plan/CHECKSUMS ) \
  || die "could not write deploy/plan/CHECKSUMS"

note "wrote deploy/plan/0001_init.sql ($(wc -l < "$plan_dir/0001_init.sql") lines)"
note "wrote deploy/plan/0001_init.json ($(wc -l < "$plan_dir/0001_init.json") lines)"
note "wrote deploy/plan/CHECKSUMS ($(wc -l < "$plan_dir/CHECKSUMS") entries)"
