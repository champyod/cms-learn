#!/usr/bin/env bash
# The control client must not be able to reach the database.
#
# cmsctl talks to the admin socket, and the socket is the only place authority is checked. A database
# crate in this binary would let a future edit read or write rows while every capability check stayed in
# the api — so the dependency list is the gate, not a comment in a document.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
manifest="$repo/cmsctl/Cargo.toml"
lock="$repo/Cargo.lock"

note() { printf '  %s\n' "$*"; }
fail() { printf 'cli gate FAILED: %s\n' "$*" >&2; exit 1; }

[ -f "$manifest" ] || fail "cmsctl/Cargo.toml is missing"

FORBIDDEN='sqlx|diesel|tokio-postgres|postgres|postgresql|rusqlite|mysql|mongodb|redis'

note "no database crate in the manifest"
if grep -Eq "^[[:space:]]*($FORBIDDEN)[[:space:]]*=" "$manifest"; then
  fail "cmsctl names a database crate: $(grep -E "^[[:space:]]*($FORBIDDEN)[[:space:]]*=" "$manifest" | tr -d ' ')"
fi

if [ -f "$lock" ]; then
  note "no database crate in the resolved tree"
  if grep -Eq "^name = \"($FORBIDDEN)\"" "$lock"; then
    fail "a database crate reached the lock file: $(grep -E "^name = \"($FORBIDDEN)\"" "$lock" | head -1)"
  fi
fi

note "the client declares the transports it is allowed"
grep -Eq "^serde_json[[:space:]]*=" "$manifest" || fail "cmsctl no longer declares serde_json"
grep -Eq "^clap[[:space:]]*=" "$manifest" || fail "cmsctl no longer declares clap"

printf 'cli gate: OK\n'
