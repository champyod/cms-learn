#!/usr/bin/env bash
# Applies the desired schema, but only after proving the file is the reviewed artifact.
#
# The desired state is applied, not a pre-generated plan: pgschema diffs the target against the file and
# applies the difference, so a re-run is a no-op and an existing database upgrades. A plan JSON carries
# the fingerprint of the database it was rendered from and refuses every other one, which is why the
# migrate cannot use it. The digest in deploy/plan/CHECKSUMS is the only link between the applied file
# and the commit that produced it, so every path that applies the schema — the schema container, `make
# upgrade`, a manual run — goes through this wrapper.
#
# Connection comes from the usual libpq environment: PGHOST, PGPORT, PGDATABASE, PGUSER, PGPASSWORD.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
plan_dir="$repo/deploy/plan"

# The pinned schema tool, resolved before any work: a version this repository did not render the plan with
# must stop the run here rather than half-apply a migration it cannot compare.
# shellcheck source=tools/pinned-pgschema.sh
. "$repo/tools/pinned-pgschema.sh"

die() {
  printf 'apply-plan: %s\n' "$*" >&2
  exit 1
}

command -v sha256sum >/dev/null 2>&1 || die "sha256sum is not installed"

[ -f "$plan_dir/0001_init.json" ] || die "$plan_dir/0001_init.json is missing"
[ -f "$plan_dir/CHECKSUMS" ] || die "$plan_dir/CHECKSUMS is missing; refusing an unverified plan"

# Verification and application must agree on which files are meant, so this check runs against the
# committed paths rather than a caller-supplied alternative.
if ! ( cd "$repo" && sha256sum --quiet --check deploy/plan/CHECKSUMS ); then
  die "the plan does not match deploy/plan/CHECKSUMS; refusing to apply it"
fi

# The desired-state diff is computed in a plan database. The schema container has no gateway, so pgschema
# cannot fetch its embedded server there; the database being migrated is also the plan database.
: "${PGHOST:?set PGHOST}" "${PGPORT:?set PGPORT}" "${PGDATABASE:?set PGDATABASE}" "${PGUSER:?set PGUSER}"
exec "$pgschema_bin" apply --file "$plan_dir/0001_init.sql" \
  --plan-host "$PGHOST" --plan-port "$PGPORT" --plan-db "$PGDATABASE" --plan-user "$PGUSER" \
  --auto-approve "$@"
