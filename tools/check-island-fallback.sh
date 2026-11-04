#!/usr/bin/env bash
# The submission form must work without the editor island, and the island it loads must be current.
#
# The island improves the textarea; it is not what makes the form post. That has to be a property of the
# generated template, not a promise: this gate reads the template and requires a `method="post"` form, a
# `source` textarea inside it and a submit button, and requires that no script tag is on the submission path.
# It also requires that the page loads the island asset, that the asset carries its licence text, and that
# the committed bundle is exactly what web/island/ builds — a stale bundle is an island that never upgrades
# while every other check is green.
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
template="${1:-$repo/api/templates/task.html}"
assets="${2:-$repo/web/public}"

fail() { printf 'island gate FAILED: %s\n' "$*" >&2; exit 1; }

[ -f "$template" ] || fail "no task template at $template"

grep -qE '<form[^>]*method="post"' "$template" \
  || fail "the task page has no form that posts"
grep -qE '<textarea[^>]*name="source"' "$template" \
  || fail "the task page has no source textarea"
grep -qE '<button[^>]*type="submit"' "$template" \
  || fail "the task page has no submit button"

# The island is markup and a script; if the submission depended on a script, this would be the place it
# showed. Only the form's own markup counts: the renderer emits one long line, so the region has to be cut
# out of it before looking, or a script in <head> would be read as if it were inside the form.
form_markup="$(grep -o '<form[^>]*>.*</form>' "$template" | head -1)"
if printf '%s' "$form_markup" | grep -q '<script'; then
  fail "the submission form depends on a script"
fi

grep -q 'src="/static/island.js"' "$template" \
  || fail "the task page does not load /static/island.js"

[ -f "$assets/island.js" ] || fail "no island bundle at $assets/island.js"
[ -f "$assets/island.LICENSE.txt" ] || fail "the island bundle has no licence text beside it"

bun run "$repo/tools/build-island.ts" --check "$assets" \
  || fail "the committed island asset is not what web/island/ builds; run: bun run tools/build-island.ts"

printf 'island gate: OK — the form posts a plain textarea, and the island asset is current\n'
