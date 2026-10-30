#!/usr/bin/env bash
# Proves the deploy gate can fail.
#
# A gate that has only ever printed "OK" is a claim, not a control. Each case below breaks one
# invariant in a throwaway copy of deploy/ and requires the gate to reject it, with the specific
# message — not merely a non-zero exit, because a gate that crashes on a typo would otherwise look
# like it was working.
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
gate="$repo/tools/check-deploy.sh"
src="$repo/deploy"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

pass=0
fail=0

# case <name> <expected substring> <mutation command>
case_of() {
  local name="$1" expect="$2" mutate="$3"
  local dir="$work/$name" before after
  rm -rf "$dir"
  cp -a "$src" "$dir"
  before="$(cd "$dir" && find . -type f -exec sha256sum {} + | sort | sha256sum)"
  ( cd "$dir" && eval "$mutate" ) || { printf 'SETUP FAILED  %s\n' "$name"; fail=$((fail+1)); return; }
  after="$(cd "$dir" && find . -type f -exec sha256sum {} + | sort | sha256sum)"

  # A mutation that silently matched nothing leaves a *healthy* stack behind, and the harness would
  # then report the gate as broken. Prove the defect was actually planted before judging the gate.
  if [ "$before" = "$after" ]; then
    printf 'SETUP FAILED  %s — mutation changed nothing (stale pattern?)\n' "$name"
    fail=$((fail+1))
    return
  fi

  local out rc
  out="$(DEPLOY_DIR="$dir" bash "$gate" 2>&1)"; rc=$?
  if [ "$rc" -eq 0 ]; then
    printf 'NOT CAUGHT    %s — gate passed a broken stack\n' "$name"
    fail=$((fail+1))
    return
  fi
  if ! printf '%s' "$out" | grep -qi -- "$expect"; then
    printf 'WRONG REASON  %s — exited %s but never said "%s"\n' "$name" "$rc" "$expect"
    printf '%s\n' "$out" | tail -3
    fail=$((fail+1))
    return
  fi
  printf 'caught        %-34s (%s)\n' "$name" "$expect"
  pass=$((pass+1))
}

# 1. the database published on every interface: the single most common way a compose file leaks data
case_of "db-published-publicly" "only proxy may face the internet" \
  "sed -i 's|127.0.0.1:5432:5432|5432:5432|' docker-compose.yml"

# 2. the judge given internet access — a submission could exfiltrate or fetch a payload
case_of "judge-with-egress" "could reach the internet" \
  "sed -i 's|networks: \[internal\].*|networks: [edge]|' docker-compose.yml"

# 3. an unpinned image: the thing you tested is not the thing that runs next week
case_of "unpinned-image" "not pinned" \
  "sed -i 's|image: postgres:18.6@sha256:[0-9a-f]*|image: postgres:latest|' docker-compose.yml"

# 4. privilege escalation left enabled on an internet-facing service
case_of "no-new-privileges-dropped" "no-new-privileges" \
  "python3 - <<'PY'
p='docker-compose.yml'
s=open(p).read()
s=s.replace('  security_opt:\\n    - no-new-privileges:true\\n  logging:\\n    driver: json-file\\n    options:\\n      max-size: \"10m\"\\n      max-file: \"3\"\\n', '  logging:\\n    driver: json-file\\n', 1)
open(p,'w').write(s)
PY"

# 5. the internal network quietly made a normal bridge
case_of "internal-network-gateway" "not marked internal" \
  "python3 - <<'PY'
p='docker-compose.yml'
s=open(p).read().replace('  internal:\\n    internal: true', '  internal:')
open(p,'w').write(s)
PY"

# 6. the Caddyfile broken in the exact way that already happened once: a third-party directive that
#    is not in Caddy's handler order
case_of "caddy-directive-unordered" "Caddyfile is not valid" \
  "sed -i '/order crowdsec first/d' Caddyfile"

# 7. the proxy publishing a non-web port: the exact pattern that has to track compose, not memory
case_of "proxy-port-not-https" "unexpected port" \
  "sed -i 's|      - \"80:8080\".*|      - \"8080:8080\"|' docker-compose.yml"

printf '\n%s case(s) caught, %s missed\n' "$pass" "$fail"
[ "$fail" -eq 0 ] || exit 1
