#!/usr/bin/env bash
# Proves the control-client gate can fail.
#
# The gate's claim is that the dependency list, not a comment in a document, is what keeps a database
# driver out of cmsctl. Each case below breaks one part of that claim in a throwaway workspace copy and
# requires the message that names the defect, not merely a non-zero exit: a gate that crashes also
# exits non-zero, and a crash proves nothing.
#
# Usage: bash tools/falsify-cli-gate.sh
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
export TMPDIR="$work"
export CARGO_TARGET_DIR="$work/target"

if ! command -v cargo >/dev/null 2>&1; then
  printf 'cannot be exercised: cargo is not on PATH\n' >&2
  exit 77
fi

pass=0
fail=0

stage() {
  local dir="$1"
  rm -rf "$dir"
  mkdir -p "$dir/tools"
  cp "$repo/Cargo.toml" "$repo/Cargo.lock" "$dir/"
  cp -a "$repo/cmsctl" "$dir/cmsctl"
  cp "$repo/tools/check-cli.sh" "$dir/tools/"
}

# case_of <name> <expected substring> <command> <mutation run inside the staged root>
case_of() {
  local name="$1" expect="$2" command="$3" mutate="$4"
  local dir="$work/$name" before after out rc
  stage "$dir"
  before="$(cd "$dir" && find . -type f -exec sha256sum {} + | sort | sha256sum)"
  ( cd "$dir" && eval "$mutate" ) || { printf 'SETUP FAILED  %s\n' "$name"; fail=$((fail + 1)); return; }
  after="$(cd "$dir" && find . -type f -exec sha256sum {} + | sort | sha256sum)"
  if [ "$before" = "$after" ]; then
    printf 'SETUP FAILED  %s — mutation changed nothing (stale pattern?)\n' "$name"
    fail=$((fail + 1))
    return
  fi
  out="$( cd "$dir" && eval "$command" 2>&1 )"
  rc=$?
  if [ "$rc" -eq 0 ]; then
    printf 'NOT CAUGHT    %s — the check passed a broken client\n' "$name"
    fail=$((fail + 1))
  elif [[ "$out" != *"$expect"* ]]; then
    printf 'WRONG MESSAGE %s — expected "%s", got:\n%s\n' "$name" "$expect" "$out"
    fail=$((fail + 1))
  else
    printf 'caught        %s\n' "$name"
    pass=$((pass + 1))
  fi
}

# Control: an untouched copy must pass, or every case below is meaningless.
control="$work/control"
stage "$control"
if ( cd "$control" && bash tools/check-cli.sh >/dev/null 2>&1 ); then
  printf 'behaved       control (an unmodified tree passes)\n'
  pass=$((pass + 1))
else
  printf 'BROKEN        control — the gate rejects a valid tree\n'
  fail=$((fail + 1))
fi

case_of "database-crate-in-manifest" "names a database crate" \
  "bash tools/check-cli.sh" \
  "printf 'sqlx = \"0.8\"\n' >> cmsctl/Cargo.toml"

case_of "database-crate-in-lockfile" "a database crate reached the lock file" \
  "bash tools/check-cli.sh" \
  "printf '\n[[package]]\nname = \"tokio-postgres\"\nversion = \"0.7.13\"\n' >> Cargo.lock"

case_of "transport-no-longer-declared" "cmsctl no longer declares serde_json" \
  "bash tools/check-cli.sh" \
  "sed -i '/^serde_json = /d' cmsctl/Cargo.toml"

case_of "source-unformatted" "cargo fmt --check reports unformatted code" \
  "bash tools/check-cli.sh" \
  "sed -i 's/^fn request_for/fn  request_for/' cmsctl/src/main.rs"

case_of "source-with-a-warning" "cargo clippy reports a warning" \
  "bash tools/check-cli.sh" \
  "sed -i 's/^use std::process::ExitCode;/use std::fmt;\nuse std::process::ExitCode;/' cmsctl/src/main.rs"

case_of "contract-test-fails" "cargo test fails" \
  "bash tools/check-cli.sh" \
  "printf '\n#[test]\nfn the_gate_probe_fails() {\n    let probe = 1 + 1;\n    assert_eq!(probe, 3);\n}\n' >> cmsctl/tests/contract.rs"

printf '\n%s case(s) behaved, %s did not\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
