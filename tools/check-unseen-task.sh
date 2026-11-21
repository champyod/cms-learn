#!/usr/bin/env bash
# The gate's name before the contract named tools/check-unseen.sh; kept as an alias so existing callers
# (package.json, the fast tier, CI) run the one implementation rather than a stale copy.
exec bash "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/check-unseen.sh" "$@"
