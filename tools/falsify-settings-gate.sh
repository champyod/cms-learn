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

# A default on a secret: a credential committed to git. The catalogue is generated JSON, so the copy is
# rewritten as JSON rather than patched textually — a textual patch would silently do nothing.
bun -e '
const fs = require("node:fs");
const catalogue = JSON.parse(fs.readFileSync(process.argv[1], "utf8"));
for (const entry of catalogue.settings) {
  if (entry.tier === "secret") { entry.default = "hunter2"; break; }
}
fs.writeFileSync(process.argv[2], JSON.stringify(catalogue, null, 2));
' "$catalogue" "$work/catalogue.json"
if bash "$gate" "$template" "$work/catalogue.json" >/dev/null 2>&1; then
  bad "a secret with a default is refused" "the gate accepted a committed credential"
else
  ok "a secret with a default is refused"
fi

# E2.2: a setting declared in code whose key deploy/schema/settings.sql can never store. The row the
# service reads cannot exist, so the gate has to name the key instead of accepting the declaration. The
# catalogue copy gains the same key first, so the older "not in the catalogue" rule cannot be the one that
# fires — the refusal must come from the schema comparison.
probe_key="Probe.bad"
bun -e '
const fs = require("node:fs");
const catalogue = JSON.parse(fs.readFileSync(process.argv[1], "utf8"));
catalogue.settings.push({ key: process.argv[3], kind: "text", tier: "operator", default: "", reload: "hot" });
fs.writeFileSync(process.argv[2], JSON.stringify(catalogue, null, 2));
' "$catalogue" "$work/catalogue.json" "$probe_key"
printf 'inline constexpr std::array k_probe_defaults{SettingDefault{"%s", ""}};\n' "$probe_key" \
  >"$work/settings_defaults.hpp"
if bun run "$repo/tools/check-settings.ts" "$work/catalogue.json" "$work/settings_defaults.hpp" \
  >"$work/schema-check.log" 2>&1; then
  bad "a setting the schema cannot store is refused and named" \
      "the gate accepted a key settings_key_shape rejects"
elif grep -q "$probe_key" "$work/schema-check.log" \
  && grep -q "settings_key_shape" "$work/schema-check.log"; then
  ok "a setting the schema cannot store is refused and named"
else
  bad "a setting the schema cannot store is refused and named" \
      "$(head -2 "$work/schema-check.log")"
fi

printf '\n%s assertion(s) behaved, %s did not\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
