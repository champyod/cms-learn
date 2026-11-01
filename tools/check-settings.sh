#!/usr/bin/env bash
# A secret setting must have no read-back path.
#
# Two places could leak one: the generated template, if the password input carried a value, and the
# catalogue, if a secret declared a default. Both are asserted here, because a form that renders an empty
# field is only safe while nothing puts a value into it.
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
template="${1:-$repo/api/templates/settings.html}"
catalogue="${2:-$repo/deploy/catalogue/settings.json}"

fail() { printf 'settings gate FAILED: %s\n' "$*" >&2; exit 1; }

[ -f "$template" ] || fail "no settings template at $template"
[ -f "$catalogue" ] || fail "no catalogue at $catalogue"

grep -q '<form[^>]*method="post"' "$template" || fail "the settings page has no form that posts"
grep -q '{% if setting.tier == "secret" %}' "$template" \
  || fail "the template does not branch on the secret tier"
grep -q '{% else %}' "$template" || fail "the template has no non-secret branch"
grep -q '{% endif %}' "$template" || fail "the template does not close the branch"

grep -q 'type="password"' "$template" || fail "the template renders no password input"

# The rule itself: the password input carries a name and no value.
password_tag="$(grep -o '<input[^>]*type="password"[^>]*>' "$template" | head -1)"
case "$password_tag" in
  *'value='*) fail "the password input carries a value attribute" ;;
esac
case "$password_tag" in
  *'name="{{ setting.key }}"'*) ;;
  *) fail "the password input is not bound to the setting key" ;;
esac

# A secret may not declare a default: a credential in a committed file is a credential in git. The catalogue
# is generated JSON, one field per line, so it is read as JSON rather than scanned as text.
secret_defaults="$(bun -e '
const fs = require("node:fs");
const catalogue = JSON.parse(fs.readFileSync(process.argv[1], "utf8"));
for (const entry of catalogue.settings) {
  if (entry.tier === "secret" && entry.default !== "") console.log(entry.key);
}' "$catalogue" 2>/dev/null)"
[ -z "$secret_defaults" ] || fail "a secret setting declares a default: $secret_defaults"

printf 'settings gate: OK — the secret tier has no read-back path\n'
