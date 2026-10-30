#!/usr/bin/env bash
# Upgrade gate: proves the deploy order and the health gate without a daemon or any image.
#
# tools/upgrade.sh is driven with stubbed docker, bun, curl and sleep, so each case fixes exactly one
# failure and asserts both the exit status and which commands did or did not run. A stub that only
# returns zero would prove nothing: the assertions are about sequence and absence.
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

pass=0
fail=0

behaved() { printf '  [ok]   %s\n' "$1"; pass=$((pass + 1)); }
broken() { printf '  [!!]   %s — %s\n' "$1" "$2"; fail=$((fail + 1)); }

stage() {
  local dir="$1"
  mkdir -p "$dir/tools" "$dir/deploy/plan" "$dir/bin"
  cp "$repo/tools/upgrade.sh" "$repo/tools/render-stack.ts" "$dir/tools/"
  cp "$repo/deploy/docker-compose.yml" "$dir/deploy/"
  printf 'DOMAIN=learn.example.com\nPOSTGRES_PASSWORD=gate-only\n' > "$dir/deploy/.env"

  cat > "$dir/bin/docker" <<'STUB'
#!/usr/bin/env bash
printf 'docker %s\n' "$*" >> "$STUB_LOG"
case "$*" in
  *".previous-compose.yml"*) exit 0 ;;
  *" pull"*) [ "${STUB_PULL_FAIL:-0}" = 1 ] && exit 1 ;;
  *"run --rm schema"*) [ "${STUB_SCHEMA_FAIL:-0}" = 1 ] && exit 1 ;;
  *"up -d --wait"*) [ "${STUB_SWITCH_FAIL:-0}" = 1 ] && exit 1 ;;
esac
exit 0
STUB

  cat > "$dir/bin/bun" <<'STUB'
#!/usr/bin/env bash
[ "${STUB_RENDER_FAIL:-0}" = 1 ] && { echo "stack drift" >&2; exit 1; }
exit 0
STUB

  cat > "$dir/bin/curl" <<'STUB'
#!/usr/bin/env bash
[ "${STUB_HEALTH_OK:-1}" = 1 ] || exit 22
exit 0
STUB

  printf '#!/usr/bin/env bash\nexit 0\n' > "$dir/bin/sleep"
  chmod +x "$dir"/bin/*
}

LAST_STATUS=0
LAST_OUT=""

run_upgrade() {
  local dir="$1" log="$2"
  LAST_OUT="$dir/out.log"
  (
    cd "$dir" || exit 1
    STUB_LOG="$log" PATH="$dir/bin:$PATH" \
      UPGRADE_HEALTH_ATTEMPTS=2 UPGRADE_HEALTH_INTERVAL=0 \
      bash "$dir/tools/upgrade.sh"
  ) >"$LAST_OUT" 2>&1
  LAST_STATUS=$?
}

expect_status() {
  local name="$1" want="$2"
  if [ "$LAST_STATUS" -eq "$want" ]; then
    behaved "$name exit $want"
  else
    broken "$name" "exit $LAST_STATUS, wanted $want; output: $(tail -2 "$LAST_OUT" | tr '\n' ' ')"
  fi
}

expect_output() {
  local name="$1" text="$2"
  if grep -q -- "$text" "$LAST_OUT"; then
    behaved "$name says \"$text\""
  else
    broken "$name" "output does not say \"$text\""
  fi
}

expect_log() {
  local name="$1" log="$2" text="$3"
  if grep -q -- "$text" "$log"; then
    behaved "$name ran \"$text\""
  else
    broken "$name" "no \"$text\" in the command log"
  fi
}

expect_no_log() {
  local name="$1" log="$2" text="$3"
  if grep -q -- "$text" "$log"; then
    broken "$name" "\"$text\" ran and should not have"
  else
    behaved "$name did not run \"$text\""
  fi
}

# 1. A good release deploys, in order, and records itself.
dir="$work/healthy"; stage "$dir"; log="$work/healthy.log"; : > "$log"
run_upgrade "$dir" "$log"
expect_status "healthy" 0
expect_log "healthy" "$log" "pull --ignore-buildable"
expect_log "healthy" "$log" "run --rm schema"
expect_log "healthy" "$log" "up -d --wait"
if [ -f "$dir/deploy/.deployed-compose.yml" ]; then
  behaved "healthy recorded the release it deployed"
else
  broken "healthy" "the deployed stack was not recorded, so the next rollback would have nothing"
fi
if [ "$(grep -n 'pull --ignore-buildable' "$log" | cut -d: -f1)" -lt "$(grep -n 'run --rm schema' "$log" | cut -d: -f1)" ] \
   && [ "$(grep -n 'run --rm schema' "$log" | cut -d: -f1)" -lt "$(grep -n 'up -d --wait' "$log" | cut -d: -f1)" ]; then
  behaved "healthy pulled, migrated, then switched"
else
  broken "healthy" "the order was not pull → migrate → switch"
fi

# 2. A hand-edited stack never reaches the registry.
dir="$work/drift"; stage "$dir"; log="$work/drift.log"; : > "$log"
STUB_RENDER_FAIL=1 run_upgrade "$dir" "$log"
expect_status "drift" 1
expect_output "drift" "not what the sources render"
expect_no_log "drift" "$log" " pull"

# 3. A failed pull leaves the running release untouched.
dir="$work/pull"; stage "$dir"; log="$work/pull.log"; : > "$log"
STUB_PULL_FAIL=1 run_upgrade "$dir" "$log"
expect_status "pull" 1
expect_output "pull" "could not be pulled"
expect_no_log "pull" "$log" "run --rm schema"
expect_no_log "pull" "$log" "up -d --wait"

# 4. A failed migration never switches the stack.
dir="$work/schema"; stage "$dir"; log="$work/schema.log"; : > "$log"
STUB_SCHEMA_FAIL=1 run_upgrade "$dir" "$log"
expect_status "schema" 1
expect_output "schema" "migration failed"
expect_no_log "schema" "$log" "up -d --wait"

# 5. A failing health check restores the previous release.
dir="$work/health"; stage "$dir"; log="$work/health.log"; : > "$log"
cp "$dir/deploy/docker-compose.yml" "$dir/deploy/.deployed-compose.yml"
STUB_HEALTH_OK=0 run_upgrade "$dir" "$log"
expect_status "health" 1
expect_output "health" "the previous release was restored"
expect_log "health" "$log" ".previous-compose.yml up -d --wait"

# 6. A stack that does not come up is also a rollback, not a half-upgraded host.
dir="$work/switch"; stage "$dir"; log="$work/switch.log"; : > "$log"
cp "$dir/deploy/docker-compose.yml" "$dir/deploy/.deployed-compose.yml"
STUB_SWITCH_FAIL=1 run_upgrade "$dir" "$log"
expect_status "switch" 1
expect_output "switch" "did not come up"
expect_log "switch" "$log" ".previous-compose.yml up -d --wait"

# 7. With nothing recorded, a failure says so instead of pretending to roll back.
dir="$work/nobackup"; stage "$dir"; log="$work/nobackup.log"; : > "$log"
STUB_HEALTH_OK=0 run_upgrade "$dir" "$log"
expect_status "nobackup" 1
expect_output "nobackup" "no previous release to restore"
expect_no_log "nobackup" "$log" ".previous-compose.yml"

printf '\n%s check(s) behaved, %s did not\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
