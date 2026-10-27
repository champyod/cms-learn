#!/usr/bin/env bash
# Proves the security scan can fail.
#
# The scanner is a third-party binary, so "it found nothing" and "it was pointed at the wrong path"
# look identical from the outside. Each case plants a defect in a throwaway directory and requires the
# scan to exit non-zero — plus one clean case that must stay green, so a scanner that simply always
# fails cannot pass this harness either.
set -uo pipefail

trivy="${TRIVY_IMAGE:-aquasec/trivy:0.66.0}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

pass=0
fail=0

scan_secrets() { docker run --rm -v "$1:/src:ro" "$trivy" fs --quiet --scanners secret --exit-code 1 /src; }
scan_config()  { docker run --rm -v "$1:/src:ro" "$trivy" config --quiet --exit-code 1 --severity MEDIUM,HIGH,CRITICAL /src; }

expect_red() {
  local name="$1" dir="$2" fn="$3"
  "$fn" "$dir" >"$work/$name.log" 2>&1
  if [ $? -eq 0 ]; then
    printf 'NOT CAUGHT    %s — scan passed a planted defect\n' "$name"
    fail=$((fail+1))
  else
    printf 'caught        %s\n' "$name"
    pass=$((pass+1))
  fi
}

expect_green() {
  local name="$1" dir="$2" fn="$3"
  if "$fn" "$dir" >"$work/$name.log" 2>&1; then
    printf 'clean         %s\n' "$name"
    pass=$((pass+1))
  else
    printf 'FALSE POSITIVE %s — scan failed on a clean tree\n' "$name"
    tail -3 "$work/$name.log"
    fail=$((fail+1))
  fi
}

# 1. a leaked token of the shape scanners actually match (not a documented example key — trivy
#    allowlists AWS's own example credentials on purpose, and a harness that used them would prove
#    nothing)
d="$work/token"; mkdir -p "$d"
printf 'token = "ghp_%s"\n' "$(head -c 36 /dev/urandom | base64 | tr -dc 'A-Za-z0-9' | head -c 36)" > "$d/config.py"
expect_red "planted-github-token" "$d" scan_secrets

# 2. a private key committed into the tree
d="$work/key"; mkdir -p "$d"
printf -- '-----BEGIN RSA PRIVATE KEY-----\n%s\n-----END RSA PRIVATE KEY-----\n' \
  "$(head -c 64 /dev/urandom | base64 | tr -d '\n')" > "$d/id_rsa"
expect_red "planted-private-key" "$d" scan_secrets

# 3. a Dockerfile that runs as root — the exact defect this repo's proxy image had
d="$work/rootuser"; mkdir -p "$d"
printf 'FROM alpine:3.21\nRUN echo hi\nCMD ["sh"]\n' > "$d/Dockerfile"
expect_red "root-user-dockerfile" "$d" scan_config

# 4. negative control: a clean tree must stay green, or case 1-3 prove nothing
d="$work/clean"; mkdir -p "$d"
printf 'FROM alpine:3.21\nRUN adduser -D app\nUSER app\nHEALTHCHECK CMD true\nCMD ["sh"]\n' > "$d/Dockerfile"
expect_green "clean-dockerfile" "$d" scan_config

printf '\n%s case(s) behaved as required, %s did not\n' "$pass" "$fail"
[ "$fail" -eq 0 ] || exit 1
