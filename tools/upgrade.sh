#!/usr/bin/env bash
# Deploys the release release.toml names. Nothing is replaced until the migration has succeeded, and a
# failure after the switch restores the stack that was running before this command started.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
deploy="$repo/deploy"
compose_file="$deploy/docker-compose.yml"
deployed_file="$deploy/.deployed-compose.yml"
previous_file="$deploy/.previous-compose.yml"
health_attempts="${UPGRADE_HEALTH_ATTEMPTS:-30}"
health_interval="${UPGRADE_HEALTH_INTERVAL:-2}"

die() { printf 'upgrade: %s\n' "$*" >&2; exit 1; }
step() { printf '  %s\n' "$*"; }
compose() { docker compose -f "$compose_file" "$@"; }

read_domain() {
  local value
  value="$(sed -n 's/^DOMAIN=//p' "$deploy/.env" | tail -1)"
  value="${value%\"}"
  value="${value#\"}"
  [ -n "$value" ] || die "DOMAIN is not set in deploy/.env"
  printf '%s' "$value"
}

# --profile migrate is what makes `config` see the schema service; without it the one container that
# can destroy data is the one container never validated.
check_sources() {
  step "the committed stack is what the sources render"
  docker compose -f "$compose_file" --profile migrate config -q \
    || die "the stack does not interpolate; nothing was switched"
  if ! command -v bun >/dev/null 2>&1; then
    step "render check skipped: bun is not installed, so CI is what proves it"
    return
  fi
  bun run "$repo/tools/render-stack.ts" --check >/dev/null \
    || die "the committed stack is not what the sources render; nothing was switched"
}

# The previous release is the file that was deployed, not the previous commit: a checkout can move
# while a release is live.
record_previous() {
  if [ -f "$deployed_file" ]; then
    cp "$deployed_file" "$previous_file"
    step "previous release kept for rollback"
    return
  fi
  rm -f "$previous_file"
  step "no previous release recorded"
}

pull() {
  step "images by digest"
  compose pull --ignore-buildable --quiet \
    || die "an image could not be pulled; nothing was switched"
}

migrate() {
  step "schema"
  compose --profile migrate run --rm schema \
    || die "the migration failed; nothing was switched"
}

# Probing through the proxy is the only check that covers TLS, routing and the api together, which is
# what a visitor actually gets.
healthy() {
  local host="$1" attempt
  for ((attempt = 1; attempt <= health_attempts; attempt += 1)); do
    curl -fsS --max-time 5 "https://${host}/healthz" >/dev/null && return 0
    sleep "$health_interval"
  done
  return 1
}

rollback() {
  local reason="$1"
  [ -f "$previous_file" ] || die "$reason, and there is no previous release to restore"
  printf 'upgrade: restoring the previous release\n' >&2
  docker compose -f "$previous_file" up -d --wait \
    || die "the previous release did not start; the stack needs attention"
  die "$reason; the previous release was restored"
}

main() {
  local host
  cd "$repo"
  [ -f "$deploy/.env" ] || die "deploy/.env is missing"
  command -v docker >/dev/null 2>&1 || die "docker is not installed"
  command -v curl >/dev/null 2>&1 || die "curl is not installed"

  check_sources
  host="$(read_domain)"
  record_previous
  pull
  migrate
  step "switch"
  compose up -d --wait || rollback "the new release did not come up"
  step "health"
  healthy "$host" || rollback "the new release failed its health check"
  cp "$compose_file" "$deployed_file"
  step "release is live at https://${host}"
}

main "$@"
