#!/usr/bin/env bash
# The shipped templates must be what the design package produces.
#
# The design package is the source; api/templates/*.html is generated from it and committed, the same way
# db/include/cms/db/tables.hpp is generated from the schema. The port runs in `--check` mode here, so this
# gate never writes: it reports that a template drifted from its component, or that a template exists with no
# page behind it.
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
target="${1:-api/templates}"

command -v bun >/dev/null 2>&1 || { echo "  skipped: bun is not installed"; exit 77; }

if bun run "$repo/tools/port-templates.ts" --check "$target"; then
  echo "templates gate: OK"
  exit 0
fi
echo "templates gate FAILED: api/templates does not match the design package" >&2
echo "  run: bun run tools/port-templates.ts" >&2
exit 1
