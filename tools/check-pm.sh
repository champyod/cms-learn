#!/usr/bin/env bash
# Package-manager gate.
#
# The repository installs with bun, and one package manager only. Two lockfiles at the root let two
# tools resolve two different trees from the same manifest while both claim to be installing it, and a
# script that shells out to another manager installs a tree the lockfile does not describe.
#
# What is asserted:
#   1. exactly one lockfile sits at the root, and it is bun.lock
#   2. no lockfile from another package manager exists in the tree git tracks
#   3. no script the repository runs invokes npm, npx, yarn or pnpm
#
# The command names are matched as whole tokens, so a word such as "npmrc" in prose passes while an
# actual `npm install` does not.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo"

fail() { printf 'package-manager gate FAILED: %s\n' "$*" >&2; exit 1; }
note() { printf '  %s\n' "$*"; }

found=()
for name in bun.lock package-lock.json yarn.lock pnpm-lock.yaml; do
  if [ -f "$name" ]; then found+=("$name"); fi
done
[ "${#found[@]}" -eq 1 ] \
  || fail "expected exactly one lockfile at the root, found ${#found[@]}: ${found[*]:-none}"
[ "${found[0]}" = bun.lock ] || fail "the root lockfile is ${found[0]}, not bun.lock"
note "one root lockfile: bun.lock"

strays="$(find . \
  \( -name .git -o -name node_modules -o -name build -o -name target -o -name dist -o -name .cache \) -prune -o \
  -type f \( -name package-lock.json -o -name yarn.lock -o -name pnpm-lock.yaml \) -print)"
if [ -n "$strays" ]; then
  printf 'package-manager gate FAILED: another package manager lockfile is in the tree:\n' >&2
  printf '%s\n' "$strays" >&2
  exit 1
fi
note "no package-lock.json, yarn.lock or pnpm-lock.yaml in the tree"

# This gate and its falsifier spell the forbidden command names out, so they are the only two files the
# invocation scan skips; every other file under tools/ is in scope.
mapfile -t scanned < <(
  printf '%s\n' package.json Makefile
  find tools -type f ! -name 'check-pm.sh' ! -name 'falsify-pm-gate.sh' | sort
)

invocations="$(grep -nE '(^|[^[:alnum:]_.-])(npm|npx|yarn|pnpm)([^[:alnum:]_.-]|$)' "${scanned[@]}" || true)"
if [ -n "$invocations" ]; then
  printf 'package-manager gate FAILED: another package manager is invoked:\n' >&2
  printf '%s\n' "$invocations" >&2
  exit 1
fi
note "${#scanned[@]} file(s) scanned; no npm, npx, yarn or pnpm invocation"

printf 'package-manager gate: OK\n'
