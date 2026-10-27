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

command -v docker >/dev/null || { echo "security scan FAILED: docker is required" >&2; exit 1; }

note "secrets and vulnerable dependencies (fixed CVEs only — an unfixable finding is not actionable)"
docker run --rm -v "$repo:/src:ro" "$trivy" fs \
  --quiet --scanners vuln,secret --ignore-unfixed --exit-code 1 \
  --skip-dirs /src/.git --skip-dirs /src/node_modules /src \
  || { echo "security scan FAILED: secret or dependency finding" >&2; exit 1; }

note "deployment hardening (Dockerfiles, compose) at MEDIUM and above"
docker run --rm -v "$repo:/src:ro" "$trivy" config \
  --quiet --exit-code 1 --severity MEDIUM,HIGH,CRITICAL /src/deploy \
  || { echo "security scan FAILED: deployment misconfiguration" >&2; exit 1; }

printf 'security scan: OK\n'
