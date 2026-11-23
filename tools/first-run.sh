#!/usr/bin/env bash
# First run: one command from a checkout on an empty host to the stack release.toml names.
#
# It is safe to run twice. Every step verifies what exists and adds only what is missing: secrets are
# generated once and never overwritten, roles are created only when absent, the plan is idempotent, an
# existing bouncer is never replaced, and `compose up -d --wait` converges.
#
# The facts it cannot invent are the operator's: DOMAIN, ACME_EMAIL, AGE_PUBLIC_KEY and
# CMSLEARN_BOOTSTRAP_ADMIN. Every one that is missing is named in a single refusal, together with a
# release that cannot be deployed and a host without docker, so an operator fixes one list rather than
# discovering the next missing fact after each attempt. Everything else is generated and recorded where
# the roles settled it: role passwords in deploy/secrets/<service>.pgpass (mode 600, mounted only into
# their service), the CrowdSec bouncer key and the operator-visible passwords in deploy/.env, and the
# seal key in deploy/host.env.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
deploy="$repo/deploy"
compose_file="$deploy/docker-compose.yml"
env_file="$deploy/.env"
host_file="$deploy/host.env"
secrets_dir="$deploy/secrets"
health_attempts="${FIRST_RUN_HEALTH_ATTEMPTS:-60}"
health_interval="${FIRST_RUN_HEALTH_INTERVAL:-5}"

SERVICES=(api generator certifier judge bank backup)
# The operator's facts: a domain and an address for ACME, a backup recipient whose private key stays
# off this host, and the first administrator the service creates.
ENV_FACTS=(DOMAIN ACME_EMAIL AGE_PUBLIC_KEY)
HOST_FACTS=(CMSLEARN_BOOTSTRAP_ADMIN)

die() { printf 'first-run: %s\n' "$*" >&2; exit 1; }
step() { printf '  %s\n' "$*"; }
compose() { docker compose -f "$compose_file" --env-file "$env_file" "$@"; }

random_hex() { head -c 32 /dev/urandom | od -An -tx1 | tr -d ' \n'; }

env_value() {
  local file="$1" key="$2"
  [ -f "$file" ] || return 0
  sed -n "s/^${key}=//p" "$file" | tail -1
}

env_or() {
  local value
  value="$(env_value "$1" "$2")"
  [ -n "$value" ] && printf '%s' "$value" || printf '%s' "$3"
}

# Replaces KEY's value in place, or appends it, without disturbing the file's other lines. The value is
# passed as an awk variable rather than sed replacement text so a key with '/', '+', '&' or '=' survives.
set_env() {
  local file="$1" key="$2" value="$3" temporary
  temporary="$(mktemp "$file.XXXXXX")"
  awk -v key="$key" -v value="$value" '
    $0 ~ "^" key "=" { print key "=" value; found = 1; next }
    { print }
    END { if (!found) print key "=" value }
  ' "$file" >"$temporary"
  chmod 600 "$temporary"
  mv "$temporary" "$file"
}

role_of() {
  case "$1" in
    api) printf 'cmslearn_api_user' ;;
    generator) printf 'cmslearn_generator_user' ;;
    certifier) printf 'cmslearn_certifier_user' ;;
    judge) printf 'cmslearn_judge_user' ;;
    bank) printf 'cmslearn_bank_user' ;;
    backup) printf 'cmslearn_backup_user' ;;
  esac
}

secret_of() { printf '%s/%s.pgpass' "$secrets_dir" "$1"; }

# The release's own deployability is a fact too: release.toml names an image `unpublished` when CI has
# not published it, and compose cannot pull what does not exist. `m5` measured this on 2026-11-21.
unpublished_release() {
  local line name="" kind="" ref=""
  while IFS= read -r line; do
    case "$line" in
      "[images."*)
        name="${line#\[images.}"
        name="${name%]}"
        kind=""
        ref=""
        ;;
      "kind ="*)
        kind="${line#kind =}"
        kind="${kind//\"/}"
        kind="${kind// /}"
        ;;
      "ref ="*)
        ref="${line#ref =}"
        ref="${ref//\"/}"
        ref="${ref// /}"
        if [ "$kind" = "unpublished" ] && [ -n "$name" ] && [ -n "$ref" ]; then
          printf '%s %s\n' "$name" "$ref"
        fi
        ;;
    esac
  done <"$repo/release.toml"
}

# One refusal for every fact the host lacks, before anything is changed on it.
preflight_facts() {
  local problems=() key unpublished
  if [ ! -f "$env_file" ]; then
    problems+=("deploy/.env is missing: copy deploy/.env.example, then set ${ENV_FACTS[*]}")
  else
    for key in "${ENV_FACTS[@]}"; do
      [ -n "$(env_value "$env_file" "$key")" ] || problems+=("deploy/.env: $key is empty")
    done
  fi
  if [ ! -f "$host_file" ]; then
    problems+=("deploy/host.env is missing: copy deploy/host.env.example, then set ${HOST_FACTS[*]}")
  else
    for key in "${HOST_FACTS[@]}"; do
      [ -n "$(env_value "$host_file" "$key")" ] || problems+=("deploy/host.env: $key is empty")
    done
  fi
  command -v docker >/dev/null 2>&1 || problems+=("docker is not installed (or not on PATH)")
  if command -v docker >/dev/null 2>&1 && ! docker compose version >/dev/null 2>&1; then
    problems+=("docker compose is not available")
  fi
  if command -v docker >/dev/null 2>&1 && ! docker info >/dev/null 2>&1; then
    problems+=("the docker daemon is not reachable")
  fi
  command -v curl >/dev/null 2>&1 || problems+=("curl is not installed (the health check uses it)")
  while read -r unpublished; do
    [ -n "$unpublished" ] || continue
    problems+=("release.toml marks ${unpublished%% *} unpublished (${unpublished#* }): the release is not deployable")
  done < <(unpublished_release)

  if [ "${#problems[@]}" -gt 0 ]; then
    printf 'first-run: this host cannot be deployed yet:\n' >&2
    printf '  - %s\n' "${problems[@]}" >&2
    exit 1
  fi
}

# A generated value goes only where its owner reads it: .env for the operator-visible passwords and the
# bouncer key, host.env for the seal key the api reads, and the secrets directory for role passwords.
fill_generated() {
  local key value
  for key in POSTGRES_PASSWORD GRAFANA_PASSWORD CMSLEARN_S3_ACCESS_KEY_ID CMSLEARN_S3_SECRET_ACCESS_KEY; do
    [ -n "$(env_value "$env_file" "$key")" ] || set_env "$env_file" "$key" "$(random_hex)"
  done
  # host.env reads the same object-store credential the store validates, so it is copied, never drawn again.
  for key in CMSLEARN_S3_ACCESS_KEY_ID CMSLEARN_S3_SECRET_ACCESS_KEY; do
    value="$(env_value "$env_file" "$key")"
    [ -n "$(env_value "$host_file" "$key")" ] || set_env "$host_file" "$key" "$value"
  done
  [ -n "$(env_value "$host_file" CMSLEARN_SEAL_KEY)" ] || set_env "$host_file" CMSLEARN_SEAL_KEY "$(random_hex)"
  # compose interpolates the whole file — the proxy's key included — before crowdsec exists to issue the
  # real one, so the first start carries a marked placeholder that is replaced as soon as the LAPI answers.
  [ -n "$(env_value "$env_file" CROWDSEC_API_KEY)" ] || set_env "$env_file" CROWDSEC_API_KEY "pending-$(random_hex)"
}

check_sources() {
  step "the stack renders from release.toml and the template"
  if ! command -v bun >/dev/null 2>&1; then
    step "render skipped: bun is not installed, so the committed render is what runs"
    return 0
  fi
  bun run "$repo/tools/render-stack.ts" >/dev/null \
    || die "deploy/docker-compose.yml does not render from release.toml; the reason is above"
}

start_database() {
  step "database"
  compose up -d --wait database || die "the database did not become healthy"
}

ensure_secrets() {
  local service role existing roles
  mkdir -p "$secrets_dir"
  chmod 700 "$secrets_dir"
  roles="$(compose exec -T -e PGPASSWORD="$(env_value "$env_file" POSTGRES_PASSWORD)" database \
    psql -h 127.0.0.1 -U "$(env_or "$env_file" POSTGRES_USER cmslearn)" \
    -d "$(env_or "$env_file" POSTGRES_DB cmslearn)" \
    -tAc "select rolname from pg_roles where rolname like 'cmslearn\\_%\\_user'" 2>/dev/null || true)"
  for service in "${SERVICES[@]}"; do
    role="$(role_of "$service")"
    if [ -f "$(secret_of "$service")" ]; then
      if ! grep -qx "$role" <<<"$roles"; then
        step "role $role is missing; creating it from $(secret_of "$service")"
      fi
      continue
    fi
    if grep -qx "$role" <<<"$roles"; then
      die "$role exists in the database and $(secret_of "$service") is missing, so its password cannot be recovered; reset that role's password and write the file, or drop the role and re-run"
    fi
    printf 'database:5432:%s:%s:%s\n' "$(env_or "$env_file" POSTGRES_DB cmslearn)" "$role" "$(random_hex)" \
      >"$(secret_of "$service")"
    chmod 600 "$(secret_of "$service")"
    step "generated $(secret_of "$service")"
  done
  # The owner's own file: the schema applies DDL as the owner and the backup reads every table, and neither
  # may carry the password in the environment. It is the operator's existing POSTGRES_PASSWORD, written as a
  # file instead of exported.
  if [ ! -f "$(secret_of owner)" ]; then
    printf 'database:5432:%s:%s:%s\n' "$(env_or "$env_file" POSTGRES_DB cmslearn)" \
      "$(env_or "$env_file" POSTGRES_USER cmslearn)" "$(env_value "$env_file" POSTGRES_PASSWORD)" \
      >"$(secret_of owner)"
    chmod 600 "$(secret_of owner)"
    step "generated $(secret_of owner)"
  fi
}

apply_roles() {
  local service password script owner database
  owner="$(env_or "$env_file" POSTGRES_USER cmslearn)"
  database="$(env_or "$env_file" POSTGRES_DB cmslearn)"
  script="$(mktemp)"
  for service in "${SERVICES[@]}"; do
    password="$(cut -d: -f5- "$(secret_of "$service")")"
    printf "\\set %s_password '%s'\n" "$service" "$password" >>"$script"
  done
  cat "$deploy/roles.sql" >>"$script"
  step "roles"
  compose exec -T -e PGPASSWORD="$(env_value "$env_file" POSTGRES_PASSWORD)" database \
    psql -h 127.0.0.1 -U "$owner" -d "$database" -q -v ON_ERROR_STOP=1 -f - <"$script" \
    || { rm -f "$script"; die "deploy/roles.sql did not apply; nothing was started beyond the database"; }
  rm -f "$script"
}

apply_plan() {
  step "schema"
  compose --profile migrate run --rm -T schema \
    || die "the schema plan did not apply; the reason is above"
}

start_crowdsec() {
  step "crowdsec, its appsec rules, and the bouncer key"
  compose up -d --wait crowdsec || die "crowdsec did not become healthy"
  if ! compose exec -T crowdsec cscli appsec-rules install crowdsecurity/crs >/dev/null 2>&1; then
    compose exec -T crowdsec cscli appsec-rules install crowdsecurity/crs \
      || die "the in-band CRS rules did not install; the reason is above"
  fi
  local current key
  current="$(env_value "$env_file" CROWDSEC_API_KEY)"
  case "$current" in
    ""|pending-*)
      if compose exec -T crowdsec cscli bouncers list -o raw 2>/dev/null \
          | awk -F, 'NR > 1 && $1 == "caddy-proxy" { found = 1 } END { exit !found }'; then
        die "crowdsec already holds the caddy-proxy bouncer and deploy/.env holds no usable key, so the key cannot be recovered; delete the bouncer (docker compose exec crowdsec cscli bouncers delete caddy-proxy) and re-run"
      fi
      key="$(compose exec -T crowdsec cscli bouncers add caddy-proxy -o raw | tr -d '\r\n')"
      [ -n "$key" ] || die "crowdsec returned no bouncer key"
      set_env "$env_file" CROWDSEC_API_KEY "$key"
      step "bouncer key recorded in deploy/.env"
      ;;
    *)
      compose exec -T crowdsec cscli bouncers list -o raw 2>/dev/null \
        | awk -F, 'NR > 1 && $1 == "caddy-proxy" { found = 1 } END { exit !found }' \
        || die "deploy/.env holds a CROWDSEC_API_KEY but crowdsec has no caddy-proxy bouncer; add it (docker compose exec crowdsec cscli bouncers add caddy-proxy -o raw) or clear the key and re-run"
      step "bouncer key already recorded"
      ;;
  esac
}

pull_and_build() {
  step "images"
  compose build || die "an image did not build; the reason is above"
  if ! compose pull --ignore-buildable --quiet; then
    step "a pinned image could not be pulled; the missing list follows"
  fi
  local image missing=()
  while IFS= read -r image; do
    [ -n "$image" ] || continue
    docker image inspect "$image" >/dev/null 2>&1 || missing+=("$image")
  done < <(compose config --images)
  if [ "${#missing[@]}" -gt 0 ]; then
    die "the release cannot start; missing image(s): ${missing[*]}"
  fi
}

bring_up() {
  local host attempt
  step "services"
  compose up -d --wait || die "a service did not come up; the reason is above"
  host="$(env_value "$env_file" DOMAIN)"
  step "waiting for https://${host}/healthz"
  for ((attempt = 1; attempt <= health_attempts; attempt += 1)); do
    if curl -fsS --max-time 5 "https://${host}/healthz" >/dev/null 2>&1; then
      step "serving at https://${host}"
      return 0
    fi
    sleep "$health_interval"
  done
  die "the stack is up but https://${host}/healthz did not answer; check the DNS record and the proxy log"
}

main() {
  cd "$repo"
  preflight_facts
  check_sources
  fill_generated
  compose --profile migrate config -q || die "the stack does not interpolate; the reason is above"
  pull_and_build
  start_database
  ensure_secrets
  apply_roles
  apply_plan
  start_crowdsec
  bring_up
  step "first run complete"
}

main "$@"
