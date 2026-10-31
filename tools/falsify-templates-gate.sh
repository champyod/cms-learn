#!/usr/bin/env bash
# Proves the templates gate can fail.
#
# Two ways it must: a template that drifted from its component, and a template with no page behind it. The
# second is the one that matters most — a renamed component that leaves its old template in place would keep
# serving markup nobody designs any more, and a gate that only compared content would call that green.
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

pass=0
fail=0
ok() { printf '  [ok]   %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  [!!]   %s — %s\n' "$1" "$2"; fail=$((fail + 1)); }

command -v bun >/dev/null 2>&1 || { echo "  skipped: bun is not installed"; exit 77; }

# A copy of the shipped templates, which the real gate must accept.
cp -R "$repo/api/templates" "$work/templates"
if bash "$repo/tools/check-templates.sh" "$work/templates" >/dev/null 2>&1; then
  ok "the shipped templates pass"
else
  bad "the shipped templates pass" "the gate refused the committed output"
fi

# One byte changed: the template no longer matches its component.
printf '\n<!-- drifted -->\n' >>"$work/templates/home.html"
if bash "$repo/tools/check-templates.sh" "$work/templates" >/dev/null 2>&1; then
  bad "a drifted template is refused" "the gate accepted a template the component does not produce"
else
  ok "a drifted template is refused"
fi

# Restore, then add a template no page produces.
cp "$repo/api/templates/home.html" "$work/templates/home.html"
cp "$repo/api/templates/home.html" "$work/templates/orphan.html"
if bash "$repo/tools/check-templates.sh" "$work/templates" >/dev/null 2>&1; then
  bad "a template with no page behind it is refused" "the gate accepted an orphan template"
else
  ok "a template with no page behind it is refused"
fi

printf '\n%s assertion(s) behaved, %s did not\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
