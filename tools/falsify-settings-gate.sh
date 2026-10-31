#!/usr/bin/env bash
# Proves the settings gate can fail.
#
# "A secret has no read-back path" is worth asserting only if the assertion dies the moment a value appears.
# Each case plants one leak — a value on the password input, a missing tier branch, a default on a secret —
# and requires the gate to refuse it.
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

pass=0
fail=0
ok() { printf '  [ok]   %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  [!!]   %s — %s\n' "$1" "$2"; fail=$((fail + 1)); }

gate="$repo/tools/check-settings.sh"
template="$repo/api/templates/settings.html"
catalogue="$repo/deploy/catalogue/settings.json"

if bash "$gate" >/dev/null 2>&1; then
  ok "the shipped settings page passes"
else
  bad "the shipped settings page passes" "the gate refused the committed output"
fi

# A value on the password input: the classic read-back leak.
sed 's|type="password" class="input input-bordered w-full" autoComplete="new-password"|type="password" value="{{ setting.value }}" class="input input-bordered w-full"|' \
  "$template" >"$work/leaky.html"
if bash "$gate" "$work/leaky.html" "$catalogue" >/dev/null 2>&1; then
  bad "a password input carrying a value is refused" "the gate accepted a read-back"
else
  ok "a password input carrying a value is refused"
fi

# No tier branch: every field would be rendered the same way.
sed 's/{% if setting.tier == "secret" %}//; s/{% else %}//; s/{% endif %}//' \
  "$template" >"$work/unbranched.html"
if bash "$gate" "$work/unbranched.html" "$catalogue" >/dev/null 2>&1; then
  bad "a template without the tier branch is refused" "the gate accepted an unbranched form"
else
  ok "a template without the tier branch is refused"
fi

# A default on a secret: a credential committed to git.
sed 's|"tier": "secret", "default": ""|"tier": "secret", "default": "hunter2"|' \
  "$catalogue" >"$work/catalogue.json"
if bash "$gate" "$template" "$work/catalogue.json" >/dev/null 2>&1; then
  bad "a secret with a default is refused" "the gate accepted a committed credential"
else
  ok "a secret with a default is refused"
fi

printf '\n%s assertion(s) behaved, %s did not\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
