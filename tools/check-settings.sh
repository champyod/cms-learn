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

# A secret may not declare a default: a credential in a committed file is a credential in git. Every key the
# service reads must exist in the catalogue, or the admin page shows no control for it — that is how the mail
# settings were found missing. Both questions read the same generated file, so one process answers both.
code_defaults="$repo/api/include/cms/api/settings_defaults.hpp"
extra=()
if [ -f "$code_defaults" ]; then
  extra+=("$code_defaults")
fi
if ! bun run "$repo/tools/check-settings.ts" "$catalogue" ${extra[@]+"${extra[@]}"}; then
  fail "the catalogue and the settings the code reads disagree"
fi

printf 'settings gate: OK — the secret tier has no read-back path\n'
