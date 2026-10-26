#!/usr/bin/env bash
# Nightly logical backup, encrypted before it leaves the process.
#
# Properties this script is built around, in order of importance:
#   1. The dump is encrypted with a public key. The private key is never on this host, so a compromise
#      of the server does not also hand over the history.
#   2. A failed pg_dump fails the run loudly (pipefail). A truncated dump written as "success" is the
#      worst outcome a backup job can produce.
#   3. Retention is bounded here, not left to whatever the volume happens to accumulate.
#   4. The offsite copy is optional but its absence is reported every run, so "we meant to configure
#      it" cannot stay quiet for months.
set -euo pipefail

: "${DATABASE_URL:?DATABASE_URL is required}"
: "${AGE_PUBLIC_KEY:?AGE_PUBLIC_KEY is required — an unencrypted dump must not be the default}"

BACKUP_DIR="${BACKUP_DIR:-/backups}"
RETAIN_DAYS="${BACKUP_RETAIN_DAYS:-7}"
BACKUP_REMOTE="${BACKUP_REMOTE:-}"
RUN_AT="${BACKUP_AT:-02:30}"

log() { printf '%s %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$*"; }

take_backup() {
  local stamp file tmp
  stamp="$(date -u +%Y%m%dT%H%M%SZ)"
  file="${BACKUP_DIR}/cms-learn-${stamp}.dump.age"
  tmp="${file}.partial"

  mkdir -p "$BACKUP_DIR"
  log "dump start → ${file}"

  # pg_dump | age: pipefail makes a dump failure (or a full disk) fail the whole pipeline instead of
  # leaving a plausible-looking partial file.
  if ! pg_dump --format=custom --no-owner --no-privileges --compress=9 "$DATABASE_URL" \
        | age --recipient "$AGE_PUBLIC_KEY" --output "$tmp"; then
    rm -f "$tmp"
    log "FAILED: pg_dump or encryption returned non-zero"
    return 1
  fi

  # An age file starts with the ASCII header "age-encryption.org/v1"; anything else is a partial
  # write or a plaintext dump. Size is checked because a 0-byte "success" is indistinguishable in a
  # file listing.
  if [ ! -s "$tmp" ] || [ "$(head -c 21 "$tmp")" != "age-encryption.org/v1" ]; then
    rm -f "$tmp"
    log "FAILED: output is not a non-empty age file"
    return 1
  fi

  mv "$tmp" "$file"
  log "dump ok: $(du -h "$file" | cut -f1)"

  find "$BACKUP_DIR" -name 'cms-learn-*.dump.age' -type f -mtime "+${RETAIN_DAYS}" -print -delete \
    | while read -r old; do log "retained-out: $(basename "$old")"; done

  if [ -n "$BACKUP_REMOTE" ]; then
    log "offsite → ${BACKUP_REMOTE}"
    if ! rclone copy "$file" "$BACKUP_REMOTE" --no-traverse; then
      log "FAILED: offsite copy"
      return 1
    fi
    log "offsite ok"
  else
    log "WARNING: BACKUP_REMOTE unset — this dump exists on one disk only"
  fi
}

seconds_until_next_run() {
  # Pure arithmetic on UTC: the container's timezone must not shift the backup window.
  local now target now_s target_s
  now_s="$(date -u +%s)"
  target="$(date -u +%Y-%m-%d)T${RUN_AT}:00Z"
  target_s="$(date -u -d "$target" +%s)"
  if [ "$target_s" -le "$now_s" ]; then
    target_s=$((target_s + 86400))
  fi
  now_s="$now_s"
  echo $((target_s - now_s))
}

log "backup sidecar up — schedule ${RUN_AT}Z, retention ${RETAIN_DAYS}d, offsite ${BACKUP_REMOTE:-none}"

# One-shot mode exists so the backup path can be tested (and so a manual backup before a risky
# migration is one command) without waiting for the nightly window.
if [ "${BACKUP_ONCE:-0}" = "1" ]; then
  log "single-shot mode"
  take_backup
  exit $?
fi

while true; do
  wait_s="$(seconds_until_next_run)"
  log "next run in ${wait_s}s"
  sleep "$wait_s"
  if take_backup; then
    log "run complete"
  else
    log "run FAILED — will retry at the next window"
  fi
done
