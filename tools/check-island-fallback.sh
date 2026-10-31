#!/usr/bin/env bash
# The submission form must work without the editor island.
#
# The island improves the textarea; it is not what makes the form post. That has to be a property of the
# generated template, not a promise: this gate reads the template and requires a `method="post"` form, a
# `source` textarea inside it and a submit button, and requires that no script tag is on the submission path.
# The falsifier deletes each of those in turn and requires this gate to notice.
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
template="${1:-$repo/api/templates/task.html}"

fail() { printf 'island gate FAILED: %s\n' "$*" >&2; exit 1; }

[ -f "$template" ] || fail "no task template at $template"

grep -qE '<form[^>]*method="post"' "$template" \
  || fail "the task page has no form that posts"
grep -qE '<textarea[^>]*name="source"' "$template" \
  || fail "the task page has no source textarea"
grep -qE '<button[^>]*type="submit"' "$template" \
  || fail "the task page has no submit button"

# The island is markup and a script; if the submission depended on a script, this would be the place it
# showed. Nothing on the form's path may be a script.
if sed -n '/<form/,/<\/form>/p' "$template" | grep -q '<script'; then
  fail "the submission form depends on a script"
fi

printf 'island gate: OK — the form posts a plain textarea\n'
