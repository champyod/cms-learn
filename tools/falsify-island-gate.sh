#!/usr/bin/env bash
# Proves the island gate can fail.
#
# "Disable the island and the form still submits" is only worth asserting if the assertion dies when the
# fallback does. Each case removes one thing the fallback needs and requires the gate to refuse it.
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

pass=0
fail=0
ok() { printf '  [ok]   %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  [!!]   %s — %s\n' "$1" "$2"; fail=$((fail + 1)); }

gate="$repo/tools/check-island-fallback.sh"
source_template="$repo/api/templates/task.html"

if bash "$gate" "$source_template" >/dev/null 2>&1; then
  ok "the shipped task page passes"
else
  bad "the shipped task page passes" "the gate refused the committed template"
fi

# No textarea: the island has nothing to upgrade and the form has nothing to post.
sed 's/<textarea[^>]*name="source"[^>]*>/<div><\/div>/' "$source_template" >"$work/no-textarea.html"
if bash "$gate" "$work/no-textarea.html" >/dev/null 2>&1; then
  bad "a page without the source textarea is refused" "the gate accepted a form with no field"
else
  ok "a page without the source textarea is refused"
fi

# A GET form: it would put the whole submission in the URL.
sed 's/method="post"/method="get"/' "$source_template" >"$work/get-form.html"
if bash "$gate" "$work/get-form.html" >/dev/null 2>&1; then
  bad "a form that does not post is refused" "the gate accepted method=get"
else
  ok "a form that does not post is refused"
fi

# A script inside the form: the submission would depend on JavaScript.
sed 's|<textarea|<script>void 0;</script><textarea|' "$source_template" >"$work/scripted.html"
if bash "$gate" "$work/scripted.html" >/dev/null 2>&1; then
  bad "a form that depends on a script is refused" "the gate accepted a scripted submission"
else
  ok "a form that depends on a script is refused"
fi

printf '\n%s assertion(s) behaved, %s did not\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
