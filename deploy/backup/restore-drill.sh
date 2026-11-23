#!/usr/bin/env bash
# Restore drill — run by the operator, with the private key mounted temporarily.
#
# A dump that has never been restored is a hypothesis. This script turns it into evidence: it
# restores the newest archive into a scratch database, reports what came back, and drops the scratch
# database again. It never touches the live database: the restore target name must differ from the
# one in DATABASE_URL, and that is enforced rather than trusted.
#
# Volume loss is the case this script exists for, so it recovers it: before restoring it recreates the
# roles the dump names (from the same credential files the services use) and restores with the dump's
# privileges, so a policy that names a role applies and the api can read what it restored.
#
# Operator's commands (deploy/README.md):
#   drill:    docker compose run --rm -v /path:/keys:ro -e AGE_KEY_FILE=/keys/<key> \
#               --entrypoint /usr/local/bin/restore-drill.sh backup
#   recovery: docker compose run --rm -v /path:/keys:ro -e AGE_KEY_FILE=/keys/<key> -e RECOVER=1 \
#               --entrypoint /usr/local/bin/restore-drill.sh backup
set -euo pipefail

: "${DATABASE_URL:?DATABASE_URL is required (points at the live database — used only for the server address)}"
: "${AGE_KEY_FILE:?AGE_KEY_FILE is required — path to the age private key}"

SECRETS_DIR="${SECRETS_DIR:-/run/cms-learn/secrets}"
ROLES_SQL="${ROLES_SQL:-/run/cms-learn/roles.sql}"
RECOVER="${RECOVER:-0}"

# The connection carries no password: libpq reads it from a file, and the file the deployment mounts is
# the owner's (it names the same role as DATABASE_URL). The documented command overrides the service's
# entrypoint, so this script installs the file itself instead of relying on that wrapper.
if [ -r /run/secrets/owner_db_password ]; then
  PGPASSFILE="${PGPASSFILE:-/tmp/owner.pgpass}"
  install -m 600 /run/secrets/owner_db_password "$PGPASSFILE"
  export PGPASSFILE
fi

ARCHIVE="${1:-}"
SCRATCH_DB="${SCRATCH_DB:-cms_learn_restore_check}"
BACKUP_DIR="${BACKUP_DIR:-/backups}"

log() { printf '%s %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$*"; }

if [ -z "$ARCHIVE" ]; then
  ARCHIVE="$(find "$BACKUP_DIR" -name 'cms-learn-*.dump.age' -type f -print0 | sort -z | tail -z -n1 | tr -d '\0')"
fi
[ -n "$ARCHIVE" ] && [ -f "$ARCHIVE" ] || { log "no archive found in ${BACKUP_DIR}"; exit 1; }

case "$ARCHIVE" in
  *.age) ;;
  *) log "refusing: ${ARCHIVE} is not an encrypted archive"; exit 1 ;;
esac

live_db="${DATABASE_URL##*/}"
live_db="${live_db%%\?*}"
admin_url="${DATABASE_URL%/*}/postgres"

# The roles the dump names (policies, grants, ownership) must exist before the restore runs, or the
# first CREATE POLICY fails on a cluster whose volume was destroyed. Their passwords are the ones the
# services already hold; nothing here invents a credential.
ensure_roles() {
  local service password vars=()
  for service in api generator certifier judge bank backup; do
    [ -r "${SECRETS_DIR}/${service}.pgpass" ] \
      || { log "FAILED: no credential file for ${service} in ${SECRETS_DIR}"; return 1; }
    password="$(cut -d: -f5- "${SECRETS_DIR}/${service}.pgpass")"
    vars+=(-v "${service}_password=${password}")
  done
  psql -X -q -v ON_ERROR_STOP=1 "${vars[@]}" "$admin_url" -f "$ROLES_SQL" >/dev/null
}

# The restore must leave a database the application can use, not rows nobody can read: the api role
# reads the table the health endpoint reads. A missing grant fails here rather than in the api's log.
verify_app_role() {
  local target="$1" count
  count="$(psql -X -A -t -v ON_ERROR_STOP=1 "$target" -c \
    "SET ROLE cmslearn_api_user; SELECT count(*) FROM settings;")"
  log "app role reads settings: ${count} row(s)"
}

if [ "$RECOVER" = "1" ]; then
  log "recovery mode: the live database ${live_db} is the target"
  target_url="$DATABASE_URL"
  target_db="$live_db"
else
  # The target must not be the live database. Comparing names rather than trusting the operator is
  # cheap; a drill that restores over production is worse than no drill.
  if [ "$SCRATCH_DB" = "$live_db" ]; then
    log "refusing: scratch database name equals the live database name (${live_db})"
    exit 1
  fi
  target_url="${DATABASE_URL%/*}/${SCRATCH_DB}"
  target_db="$SCRATCH_DB"
fi

log "archive: ${ARCHIVE} ($(du -h "$ARCHIVE" | cut -f1))"
ensure_roles || { log "FAILED: the roles the dump names could not be recreated"; exit 1; }
log "roles recreated from ${SECRETS_DIR}"

if [ "$RECOVER" = "1" ]; then
  if [ "$(psql -X -A -t "$admin_url" -c "SELECT 1 FROM pg_database WHERE datname='${target_db}'")" != "1" ]; then
    psql -X -q "$admin_url" -c "CREATE DATABASE ${target_db};" >/dev/null
  fi
  cleanup() { :; }
  trap cleanup EXIT
else
  cleanup() {
    psql -X -q "$admin_url" -c "DROP DATABASE IF EXISTS ${SCRATCH_DB};" >/dev/null 2>&1 || true
  }
  trap cleanup EXIT
  cleanup
  psql -X -q "$admin_url" -c "CREATE DATABASE ${SCRATCH_DB};" >/dev/null
fi

log "restoring into ${target_db} with the dump's privileges"
if ! age --decrypt --identity "$AGE_KEY_FILE" "$ARCHIVE" \
      | pg_restore --no-owner --clean --if-exists --exit-on-error --dbname "$target_url"; then
  log "FAILED: restore returned non-zero"
  exit 1
fi

tables="$(psql -X -A -t "$target_url" -c \
  "SELECT count(*) FROM information_schema.tables WHERE table_schema='public';")"
log "restored tables: ${tables}"
verify_app_role "$target_url" || { log "FAILED: the restored database is not readable by the api role"; exit 1; }

if [ "${RESTORE_ASSERT_TABLES:-}" != "" ]; then
  for t in $RESTORE_ASSERT_TABLES; do
    n="$(psql -X -A -t "$target_url" -c "SELECT count(*) FROM ${t};")"
    log "  ${t}: ${n} rows"
    if [ "$n" -eq 0 ]; then
      log "FAILED: ${t} restored empty"
      exit 1
    fi
  done
fi

log "restore drill PASSED"
