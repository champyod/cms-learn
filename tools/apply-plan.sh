#!/usr/bin/env bash
# Applies the rendered schema plan, but only after proving it is the reviewed artifact.
#
# pgschema executes a plan it is handed without checking it: an edited step applies as written, and
# only fails if the database itself rejects the result. The digest in deploy/plan/CHECKSUMS is the
# only link between those files and the commit that produced them, so every path that applies the
# schema — the schema container, `make upgrade`, a manual run — goes through this wrapper.
#
# Connection comes from the usual libpq environment: PGHOST, PGPORT, PGDATABASE, PGUSER, PGPASSWORD.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
plan_dir="$repo/deploy/plan"

die() {
  printf 'apply-plan: %s\n' "$*" >&2
  exit 1
}

command -v pgschema >/dev/null 2>&1 || die "pgschema is not installed"
command -v sha256sum >/dev/null 2>&1 || die "sha256sum is not installed"

[ -f "$plan_dir/0001_init.json" ] || die "$plan_dir/0001_init.json is missing"
[ -f "$plan_dir/CHECKSUMS" ] || die "$plan_dir/CHECKSUMS is missing; refusing an unverified plan"

# Verification and application must agree on which files are meant, so this check runs against the
# committed paths rather than a caller-supplied alternative.
if ! ( cd "$repo" && sha256sum --quiet --check deploy/plan/CHECKSUMS ); then
  die "the plan does not match deploy/plan/CHECKSUMS; refusing to apply it"
fi

exec pgschema apply --plan "$plan_dir/0001_init.json" --auto-approve "$@"
