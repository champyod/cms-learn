#!/usr/bin/env bash
# Restore drill — run by the operator, with the private key mounted temporarily.
#
# A dump that has never been restored is a hypothesis. This script turns it into evidence: it
# restores the newest archive into a scratch database, reports what came back, and drops the scratch
# database again. It never touches the live database: the restore target name must differ from the
# one in DATABASE_URL, and that is enforced rather than trusted.
set -euo pipefail

: "${DATABASE_URL:?DATABASE_URL is required (points at the live database — used only for the server address)}"
: "${AGE_KEY_FILE:?AGE_KEY_FILE is required — path to the age private key}"

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

# The target must not be the live database. Comparing names rather than trusting the operator is
# cheap; a drill that restores over production is worse than no drill.
live_db="${DATABASE_URL##*/}"
live_db="${live_db%%\?*}"
if [ "$SCRATCH_DB" = "$live_db" ]; then
  log "refusing: scratch database name equals the live database name (${live_db})"
  exit 1
fi

admin_url="${DATABASE_URL%/*}/postgres"
scratch_url="${DATABASE_URL%/*}/${SCRATCH_DB}"

cleanup() {
  psql -X -q "$admin_url" -c "DROP DATABASE IF EXISTS ${SCRATCH_DB};" >/dev/null 2>&1 || true
}
trap cleanup EXIT

log "archive: ${ARCHIVE} ($(du -h "$ARCHIVE" | cut -f1))"
log "restoring into scratch database ${SCRATCH_DB}"

cleanup
psql -X -q "$admin_url" -c "CREATE DATABASE ${SCRATCH_DB};" >/dev/null

if ! age --decrypt --identity "$AGE_KEY_FILE" "$ARCHIVE" \
      | pg_restore --no-owner --no-privileges --exit-on-error --dbname "$scratch_url"; then
  log "FAILED: restore returned non-zero"
  exit 1
fi

tables="$(psql -X -A -t "$scratch_url" -c \
  "SELECT count(*) FROM information_schema.tables WHERE table_schema='public';")"
log "restored tables: ${tables}"

if [ "${RESTORE_ASSERT_TABLES:-}" != "" ]; then
  for t in $RESTORE_ASSERT_TABLES; do
    n="$(psql -X -A -t "$scratch_url" -c "SELECT count(*) FROM ${t};")"
    log "  ${t}: ${n} rows"
    if [ "$n" -eq 0 ]; then
      log "FAILED: ${t} restored empty"
      exit 1
    fi
  done
fi

log "restore drill PASSED"
