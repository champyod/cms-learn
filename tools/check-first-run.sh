#!/usr/bin/env bash
# First-run gate: proves the one command's order, refusals and idempotence without a daemon or an image.
#
# tools/first-run.sh is driven with stubbed docker, bun and curl, so each case fixes one fact and asserts
# the exit status, the message and which commands did or did not run. A stub that only returns zero would
# prove nothing: the assertions are about sequence, absence and the files the run leaves behind.
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

pass=0
fail=0
skip=0
behaved() { printf '  [ok]   %s\n' "$1"; pass=$((pass + 1)); }
broken() { printf '  [!!]   %s — %s\n' "$1" "$2"; fail=$((fail + 1)); }
skipped() { printf '  [--]   %s — %s\n' "$1" "$2"; skip=$((skip + 1)); }

DEPLOYABLE_RELEASE='version = "0.0.0"

[images.proxy]
kind = "built"
ref = "cms-learn-proxy"
context = "proxy"

[images.backup]
kind = "built"
ref = "cms-learn-backup"
context = "backup"

[images.schema]
kind = "built"
ref = "cms-learn-schema"
context = "schema"

[images.api]
kind = "built"
ref = "cms-learn-api"
context = "api"

[images.judge]
kind = "built"
ref = "cms-learn-judge"
context = "judge"

[images.database]
kind = "pinned"
ref = "postgres:18.6"
digest = "sha256:1111111111111111111111111111111111111111111111111111111111111111"
'

UNPUBLISHED_RELEASE='version = "0.0.0"

[images.api]
kind = "unpublished"
ref = "ghcr.io/champyod/cms-learn-api"

[images.judge]
kind = "unpublished"
ref = "ghcr.io/champyod/cms-learn-judge"
'

stage() {
  local dir="$1"
  mkdir -p "$dir/tools" "$dir/deploy" "$dir/bin"
  cp "$repo/tools/first-run.sh" "$repo/tools/render-stack.ts" "$dir/tools/"
  cp "$repo/deploy/roles.sql" "$repo/deploy/docker-compose.yml" "$dir/deploy/"
  printf '%s\n' "$DEPLOYABLE_RELEASE" >"$dir/release.toml"

  cat >"$dir/bin/docker" <<'STUB'
#!/usr/bin/env bash
case "$*" in
  "compose version"|"info") exit 0 ;;
esac
printf 'docker %s\n' "$*" >>"${STUB_LOG:?}"
case "$*" in
  *"config --images") for image in ${STUB_IMAGES:-image-one image-two}; do printf '%s\n' "$image"; done; exit 0 ;;
  *" run --rm -T schema") [ "${STUB_SCHEMA_FAIL:-0}" = 1 ] && { echo "sed: can't read docs/dependencies.md" >&2; exit 2; }; exit 0 ;;
  *" build") [ "${STUB_BUILD_FAIL:-0}" = 1 ] && exit 1; exit 0 ;;
  *" pull --ignore-buildable"*) [ "${STUB_PULL_FAIL:-0}" = 1 ] && exit 1; exit 0 ;;
  *"up -d --wait database"*) [ "${STUB_DB_FAIL:-0}" = 1 ] && exit 1; exit 0 ;;
  *"up -d --wait crowdsec"*) [ "${STUB_CROWDSEC_FAIL:-0}" = 1 ] && exit 1; exit 0 ;;
  *"up -d --wait"*) [ "${STUB_UP_FAIL:-0}" = 1 ] && exit 1; exit 0 ;;
  *"appsec-rules install"*) [ "${STUB_APPSEC_FAIL:-0}" = 1 ] && exit 1; exit 0 ;;
  *"bouncers list -o raw")
    printf 'name,ip,revoked,last_pull,type,version,auth_type\n'
    [ "${STUB_BOUNCER_EXISTS:-0}" = 1 ] && printf 'caddy-proxy,,validated,,,,api-key\n'
    exit 0
    ;;
  *"bouncers add caddy-proxy -o raw") printf '%s' "${STUB_KEY:-generated-bouncer-key}"; exit 0 ;;
  *"psql"*"-tAc"*) printf '%s\n' "${STUB_ROLES:-}"; exit 0 ;;
  *"psql"*) cat >/dev/null; exit 0 ;;
  *"image inspect"*)
    [ "$3" = "${STUB_MISSING_IMAGE:-}" ] && exit 1
    exit 0
    ;;
esac
exit 0
STUB

  cat >"$dir/bin/bun" <<'STUB'
#!/usr/bin/env bash
exit 0
STUB
  cat >"$dir/bin/curl" <<'STUB'
#!/usr/bin/env bash
printf 'curl %s\n' "$*" >>"${STUB_LOG:?}"
[ "${STUB_HEALTH_FAIL:-0}" = 1 ] && exit 22
exit 0
STUB
  printf '#!/usr/bin/env bash\nexit 0\n' >"$dir/bin/sleep"
  chmod +x "$dir"/bin/*
}

facts() {
  local dir="$1"
  cat >"$dir/deploy/.env" <<'ENV'
DOMAIN=learn.example.com
ACME_EMAIL=ops@example.com
TAG=dev
POSTGRES_DB=cmslearn
POSTGRES_USER=cmslearn
POSTGRES_PASSWORD=
GRAFANA_PASSWORD=
CMSLEARN_S3_ACCESS_KEY_ID=
CMSLEARN_S3_SECRET_ACCESS_KEY=
CROWDSEC_API_KEY=
AGE_PUBLIC_KEY=age1gateonlygateonlygateonlygateonlygateonlygateonly
ENV
  cat >"$dir/deploy/host.env" <<'ENV'
CMSLEARN_DATABASE_URL=postgres://cmslearn_api_user@database:5432/cmslearn
CMSLEARN_SEAL_KEY=
CMSLEARN_BOOTSTRAP_ADMIN=operator@example.com
CMSLEARN_S3_ACCESS_KEY_ID=
CMSLEARN_S3_SECRET_ACCESS_KEY=
ENV
  chmod 600 "$dir/deploy/.env" "$dir/deploy/host.env"
}

LAST_STATUS=0
LAST_OUT=""

run_first() {
  local dir="$1" log="$2" extra="${3:-}"
  LAST_OUT="$dir/out.log"
  (
    cd "$dir" || exit 1
    # shellcheck disable=SC2086
    env $extra STUB_LOG="$log" PATH="$dir/bin:$PATH" \
      FIRST_RUN_HEALTH_ATTEMPTS=2 FIRST_RUN_HEALTH_INTERVAL=0 \
      bash "$dir/tools/first-run.sh"
  ) >"$LAST_OUT" 2>&1
  LAST_STATUS=$?
}

expect_status() {
  local name="$1" want="$2"
  if [ "$LAST_STATUS" -eq "$want" ]; then
    behaved "$name exit $want"
  else
    broken "$name" "exit $LAST_STATUS, wanted $want; output: $(tail -3 "$LAST_OUT" | tr '\n' ' ')"
  fi
}

expect_output() {
  local name="$1" text="$2"
  if grep -qF -- "$text" "$LAST_OUT"; then
    behaved "$name"
  else
    broken "$name" "output does not name \"$text\": $(tail -3 "$LAST_OUT" | tr '\n' ' ')"
  fi
}

expect_absent() {
  local name="$1" text="$2"
  if grep -qF -- "$text" "$LAST_OUT"; then
    broken "$name" "output names \"$text\""
  else
    behaved "$name"
  fi
}

expect_order() {
  local name="$1" first="$2" second="$3" line_first line_second
  line_first="$(grep -nF -- "$first" "$LAST_LOG" | head -1 | cut -d: -f1)"
  line_second="$(grep -nF -- "$second" "$LAST_LOG" | head -1 | cut -d: -f1)"
  if [ -n "$line_first" ] && [ -n "$line_second" ] && [ "$line_first" -lt "$line_second" ]; then
    behaved "$name"
  else
    broken "$name" "order: \"$first\" at ${line_first:-none}, \"$second\" at ${line_second:-none}"
  fi
}

# Case 1: the refusal names every fact the host lacks, in one message, before changing anything.
bad_facts="$(mktemp -d)"
stage "$bad_facts"
LAST_LOG="$bad_facts/docker.log"
run_first "$bad_facts" "$LAST_LOG"
expect_status "an empty host is refused" 1
for fact in DOMAIN ACME_EMAIL AGE_PUBLIC_KEY CMSLEARN_BOOTSTRAP_ADMIN deploy/.env deploy/host.env; do
  expect_output "the refusal names $fact" "$fact"
done
if [ -s "$LAST_LOG" ]; then
  broken "nothing is changed before the facts exist" "docker ran: $(head -2 "$LAST_LOG" | tr '\n' ' ')"
else
  behaved "nothing is changed before the facts exist"
fi
[ ! -e "$bad_facts/deploy/secrets" ] && behaved "the refusal writes no secret" || broken "the refusal writes no secret" "deploy/secrets exists"

# Case 2: a release that is not deployable is refused by name, still before anything changes.
unpublished="$(mktemp -d)"
stage "$unpublished"
printf '%s\n' "$UNPUBLISHED_RELEASE" >"$unpublished/release.toml"
facts "$unpublished"
LAST_LOG="$unpublished/docker.log"
run_first "$unpublished" "$LAST_LOG"
expect_status "an unpublished release is refused" 1
expect_output "the refusal names api" "api"
expect_output "the refusal names judge" "judge"
expect_output "the refusal says why" "unpublished"

# Case 3: a deployable release with facts runs the order and leaves the secrets in their files.
happy="$(mktemp -d)"
stage "$happy"
facts "$happy"
LAST_LOG="$happy/docker.log"
run_first "$happy" "$LAST_LOG"
expect_status "a deployable release is brought up" 0
expect_output "the stack is serving" "serving at https://learn.example.com"
expect_order "the database starts before the roles" "up -d --wait database" "psql"
expect_order "the roles are applied before the plan" "psql" "run --rm -T schema"
expect_order "the plan is applied before crowdsec" "run --rm -T schema" "up -d --wait crowdsec"
expect_order "the appsec rules are installed before the bouncer" "appsec-rules install" "bouncers add caddy-proxy"
expect_order "the bouncer exists before the health check" "bouncers add caddy-proxy" "curl -fsS"
expect_order "the images are handled before the database starts" " build" "up -d --wait database"
# Six service roles, plus the owner's own file: the schema applies DDL as the owner and the backup reads
# every table, and neither may carry the password in the environment.
secret_count="$(find "$happy/deploy/secrets" -name '*.pgpass' 2>/dev/null | wc -l)"
[ "$secret_count" -eq 7 ] && behaved "six role secret files and the owner's file are written" || broken "six role secret files and the owner's file are written" "found $secret_count"
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    skipped "the role secret files are mode 600" "$(uname -s) cannot represent POSIX modes"
    ;;
  *)
    mode_bad="$(find "$happy/deploy/secrets" -name '*.pgpass' ! -perm 600 -print -quit 2>/dev/null)"
    [ -z "$mode_bad" ] && behaved "the role secret files are mode 600" || broken "the role secret files are mode 600" "$mode_bad"
    ;;
esac
role_password="$(cut -d: -f5- "$happy/deploy/secrets/api.pgpass")"
if [ -n "$role_password" ] && ! grep -qF -- "$role_password" "$happy/deploy/.env" && ! grep -qF -- "$role_password" "$happy/deploy/host.env"; then
  behaved "a role password stays in its mounted file"
else
  broken "a role password stays in its mounted file" "the password appears in an environment file"
fi
grep -q '^CROWDSEC_API_KEY=generated-bouncer-key$' "$happy/deploy/.env" \
  && behaved "the obtained bouncer key is recorded" || broken "the obtained bouncer key is recorded" "$(grep '^CROWDSEC_API_KEY' "$happy/deploy/.env")"
grep -q '^POSTGRES_PASSWORD=.\+' "$happy/deploy/.env" \
  && behaved "the generated database password is recorded" || broken "the generated database password is recorded" "POSTGRES_PASSWORD is empty"
grep -q '^CMSLEARN_SEAL_KEY=.\+' "$happy/deploy/host.env" \
  && behaved "the generated seal key is recorded" || broken "the generated seal key is recorded" "CMSLEARN_SEAL_KEY is empty"
grep -q '^CMSLEARN_S3_ACCESS_KEY_ID=.\+' "$happy/deploy/host.env" \
  && behaved "host.env carries the store credential" || broken "host.env carries the store credential" "the store credential is empty"

# Case 4: running it again changes no secret and adds no second bouncer.
hash_before="$(find "$happy/deploy/secrets" "$happy/deploy/.env" "$happy/deploy/host.env" -type f -exec cksum {} + | LC_ALL=C sort)"
bouncer_before="$(grep -c 'bouncers add caddy-proxy' "$LAST_LOG" || true)"
LAST_LOG="$happy/docker2.log"
run_first "$happy" "$LAST_LOG" "STUB_BOUNCER_EXISTS=1"
expect_status "a second run converges" 0
hash_after="$(find "$happy/deploy/secrets" "$happy/deploy/.env" "$happy/deploy/host.env" -type f -exec cksum {} + | LC_ALL=C sort)"
[ "$hash_before" = "$hash_after" ] && behaved "a second run rewrites no secret" || broken "a second run rewrites no secret" "a secret file changed"
bouncer_after="$(grep -c 'bouncers add caddy-proxy' "$LAST_LOG" || true)"
[ "$bouncer_after" = 0 ] && [ "$bouncer_before" = 1 ] \
  && behaved "the bouncer is added exactly once, across both runs" \
  || broken "the bouncer is added exactly once, across both runs" "first $bouncer_before, second $bouncer_after"

# Case 5: a lost key is refused with the reason and the remedy, not regenerated.
lost="$(mktemp -d)"
stage "$lost"
facts "$lost"
sed -i 's/^CROWDSEC_API_KEY=.*/CROWDSEC_API_KEY=pending-lost/' "$lost/deploy/.env"
LAST_LOG="$lost/docker.log"
run_first "$lost" "$LAST_LOG" "STUB_BOUNCER_EXISTS=1"
expect_status "a bouncer without its key is refused" 1
expect_output "the refusal names the bouncer" "caddy-proxy"
expect_output "the refusal says the key cannot be recovered" "cannot be recovered"
expect_output "the refusal gives the remedy" "bouncers delete"

# Case 6: a role that exists without its file is refused, because its password cannot be recovered.
orphan="$(mktemp -d)"
stage "$orphan"
facts "$orphan"
LAST_LOG="$orphan/docker.log"
run_first "$orphan" "$LAST_LOG" "STUB_ROLES=cmslearn_api_user"
expect_status "an orphaned role is refused" 1
expect_output "the refusal names the role" "cmslearn_api_user"
expect_output "the refusal says the password is unrecoverable" "cannot be recovered"

# Case 7: the plan's own failure is passed through, not retried or defaulted.
schema_fail="$(mktemp -d)"
stage "$schema_fail"
facts "$schema_fail"
LAST_LOG="$schema_fail/docker.log"
run_first "$schema_fail" "$LAST_LOG" "STUB_SCHEMA_FAIL=1"
expect_status "a failed plan stops the run" 1
expect_output "the plan failure is named" "schema plan did not apply"
expect_absent "the stack is not started after a failed plan" "serving at https://"

# Case 8: a missing image is named before the stack is asked to start.
missing="$(mktemp -d)"
stage "$missing"
facts "$missing"
LAST_LOG="$missing/docker.log"
run_first "$missing" "$LAST_LOG" "STUB_MISSING_IMAGE=image-two"
expect_status "a missing image stops the run" 1
expect_output "the missing image is named" "image-two"
expect_absent "no service is started with a missing image" "serving at https://"

# Case 9: an unhealthy edge is a failure, not a success.
unhealthy="$(mktemp -d)"
stage "$unhealthy"
facts "$unhealthy"
LAST_LOG="$unhealthy/docker.log"
run_first "$unhealthy" "$LAST_LOG" "STUB_HEALTH_FAIL=1"
expect_status "a stack that does not answer is refused" 1
expect_output "the health failure is named" "healthz did not answer"

printf '\n%s assertion(s) behaved, %s skipped, %s did not\n' "$pass" "$skip" "$fail"
[ "$fail" -eq 0 ]
