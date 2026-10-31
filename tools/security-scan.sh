#!/usr/bin/env bash
# Security scan gate.
#
# Two questions, both answered by a pinned scanner rather than by hope:
#   1. does anything in this tree leak a credential, or depend on a library with a known CVE?
#   2. do the deployment files themselves break a hardening rule (root user, no healthcheck, ...)?
#
# The image tag is pinned: a scanner that changes under you turns a green build into a mystery red.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
trivy="${TRIVY_IMAGE:-aquasec/trivy:0.66.0}"

note() { printf '  %s\n' "$*"; }

# A scanner that could not run has not found anything. Reporting the two as one message is how a
# network outage becomes a security finding nobody can act on, so the reason is separated here.
scan() {
  local label="$1"
  shift
  local output status
  set +e
  output="$("$@" 2>&1)"
  status=$?
  set -e
  [ -n "$output" ] && printf '%s\n' "$output"
  [ "$status" -eq 0 ] && return 0
  if printf '%s' "$output" | grep -qE 'FATAL|failed to download|failed to load|context deadline exceeded|no such image'; then
    echo "security scan FAILED: $label could not run — the scanner never produced a verdict" >&2
  else
    echo "security scan FAILED: $label reported a finding" >&2
  fi
  exit 1
}

command -v docker >/dev/null || { echo "security scan FAILED: docker is required" >&2; exit 1; }

note "secrets and vulnerable dependencies (fixed CVEs only — an unfixable finding is not actionable)"
scan "the vulnerability and secret scan" \
  docker run --rm -v "$repo:/src:ro" "$trivy" fs \
  --scanners vuln,secret --ignore-unfixed --exit-code 1 \
  --skip-dirs /src/.git --skip-dirs /src/node_modules /src

note "deployment hardening (Dockerfiles, compose) at MEDIUM and above"
scan "the deployment configuration scan" \
  docker run --rm -v "$repo:/src:ro" "$trivy" config \
  --exit-code 1 --severity MEDIUM,HIGH,CRITICAL /src/deploy

printf 'security scan: OK\n'
