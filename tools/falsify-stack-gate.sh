#!/usr/bin/env bash
# Proves the stack gate can fail.
#
# A gate that has only ever printed a count is a claim, not a control. Each case below breaks one
# thing in a throwaway copy of release.toml and deploy/, and requires the gate to reject it with the
# specific message — not merely a non-zero exit, because a harness that crashes looks like it worked.
#
# Usage: bash tools/falsify-stack-gate.sh
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
gate="$repo/tools/render-stack.ts"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

pass=0
fail=0

stage() {
  local dir="$1"
  rm -rf "$dir"
  mkdir -p "$dir"
  cp -a "$repo/release.toml" "$dir/"
  cp -a "$repo/deploy" "$dir/"
}

# case <name> <expected substring> <mutation command, run inside the staged root>
case_of() {
  local name="$1" expect="$2" mutate="$3"
  local dir="$work/$name" before after out rc
  stage "$dir"
  before="$(cd "$dir" && find . -type f -exec sha256sum {} + | sort | sha256sum)"
  ( cd "$dir" && eval "$mutate" ) || { printf 'SETUP FAILED  %s\n' "$name"; fail=$((fail + 1)); return; }
  after="$(cd "$dir" && find . -type f -exec sha256sum {} + | sort | sha256sum)"

  # A mutation that matched nothing leaves a healthy stack behind, and the harness would then report
  # the gate as broken. Prove the defect was planted before judging the gate.
  if [ "$before" = "$after" ]; then
    printf 'SETUP FAILED  %s — mutation changed nothing (stale pattern?)\n' "$name"
    fail=$((fail + 1))
    return
  fi

  out="$(bun run "$gate" --check --root "$dir" 2>&1)"
  rc=$?
  if [ "$rc" -eq 0 ]; then
    printf 'NOT CAUGHT    %s — gate passed a broken release\n' "$name"
    fail=$((fail + 1))
  elif [[ "$out" != *"$expect"* ]]; then
    printf 'WRONG MESSAGE %s — expected "%s", got:\n%s\n' "$name" "$expect" "$out"
    fail=$((fail + 1))
  else
    printf 'caught        %s\n' "$name"
    pass=$((pass + 1))
  fi
}

# Control: an untouched copy must pass, or every case below would be meaningless.
control="$work/control"
stage "$control"
if bun run "$gate" --check --root "$control" >/dev/null 2>&1; then
  printf 'behaved       control (an unmodified release passes)\n'
  pass=$((pass + 1))
else
  printf 'BROKEN        control — the gate rejects a valid release\n'
  fail=$((fail + 1))
fi

case_of "digest-drift" "is not what release.toml produces" \
  "sed -i 's/^digest = \"sha256:979bcc.*/digest = \"sha256:1111111111111111111111111111111111111111111111111111111111111111\"/' release.toml"

case_of "hand-edited-compose" "is not what release.toml produces" \
  "sed -i 's/max-size: \"10m\"/max-size: \"12m\"/' deploy/docker-compose.yml"

case_of "service-needs-missing-image" "absent from release.toml" \
  "sed -i 's/^\[images\.grafana\]/[images.grafana-renamed]/' release.toml"

case_of "image-nobody-uses" "declared but no service uses it" \
  "printf '\n[images.spare]\nkind = \"unpublished\"\nref = \"example/spare\"\n' >> release.toml"

case_of "pinned-without-digest" "a pinned image needs sha256" \
  "sed -i 's/^digest = \"sha256:fa3302.*/digest = \"sha256:notadigest\"/' release.toml"

case_of "built-context-missing" "has no Dockerfile at" \
  "sed -i 's/^context = \"proxy\"/context = \"proxy-that-is-not-here\"/' release.toml"

printf '\n%s case(s) behaved, %s did not\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
