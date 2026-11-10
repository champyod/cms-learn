#!/usr/bin/env bash
# The one place that decides which `pgschema` runs.
#
# The committed plan in deploy/plan is only comparable to a render made by the version that rendered it. A
# gate that takes whatever is first on PATH therefore fails as "drift" on a host where the tool was upgraded,
# which says nothing about the plan and everything about the host. This resolves the version
# docs/dependencies.md pins, and when the only binary it can find is another version it refuses — before
# `initdb`, before a render and before an apply — naming both versions and where the pin is written.
#
# Sourced by tools/apply-plan.sh, tools/check-schema.sh and tools/render-plan.sh. It sets:
#   pgschema_bin   absolute path to a binary that reports the pinned version
# and exits non-zero otherwise. It never falls back to another version: a fallback is how the plan silently
# stops matching its own render.

cms_pgschema_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cms_pgschema_doc="$cms_pgschema_dir/../docs/dependencies.md"

cms_pgschema_refuse() {
  printf 'schema tool refused: %s\n' "$*" >&2
  exit 1
}

# The pin is the version column of the Tools table, so the document an operator reads and the version the
# gates demand are one fact rather than two.
cms_pgschema_pinned() {
  sed -n 's/^| *pgschema *| *\**\([0-9][0-9.]*\)\** *|.*/\1/p' "$cms_pgschema_doc" | head -1
}

cms_pgschema_reports() {  # cms_pgschema_reports <path> ; prints the version it reports, empty when it cannot
  "$1" --version 2>/dev/null | sed -n 's/.*\([0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\).*/\1/p' | head -1
}

# Best-effort discovery of a locally cached package: a convenience, never a requirement.
cms_pgschema_local_cache() {
  local pin="$1" dir candidate
  local caches=(
    "$HOME/.cache/yay/pgschema-bin"
    "$HOME/.cache/paru/clone/pgschema-bin"
    /var/cache/pacman/pkg
    /var/cache/apt/archives
    /var/cache/dnf
    /var/cache/yum
    "$HOME/Library/Caches/Homebrew"
  )
  for dir in "${caches[@]}"; do
    [ -d "$dir" ] || continue
    for candidate in "$dir"/pgschema*"$pin"*; do
      [ -x "$candidate" ] || continue
      [ "$(cms_pgschema_reports "$candidate")" = "$pin" ] || continue
      printf '%s\n' "$candidate"
      return 0
    done
  done
  return 1
}

cms_pgschema_resolve() {
  local pin="${1:-}" candidate="" found="" looked=""
  [ -n "$pin" ] || pin="$(cms_pgschema_pinned)"
  [ -n "$pin" ] || cms_pgschema_refuse "docs/dependencies.md names no pgschema version; the pin lives in its Tools table"

  local candidates=()
  [ -n "${CMSLEARN_PGSCHEMA:-}" ] && candidates+=("$CMSLEARN_PGSCHEMA")
  local on_path
  on_path="$(command -v pgschema || true)"
  [ -n "$on_path" ] && candidates+=("$on_path")
  local cached=""
  cached="$(cms_pgschema_local_cache "$pin")" || cached=""
  [ -n "$cached" ] && candidates+=("$cached")

  for candidate in "${candidates[@]}"; do
    [ -x "$candidate" ] || continue
    found="$(cms_pgschema_reports "$candidate")"
    if [ "$found" = "$pin" ]; then
      pgschema_bin="$candidate"
      return 0
    fi
    looked="${looked}${looked:+, }$candidate (reports ${found:-no version})"
  done

  cms_pgschema_refuse "docs/dependencies.md pins pgschema ${pin}, and no binary reporting that version was found.
  looked at: ${looked:-nothing executable; set CMSLEARN_PGSCHEMA or put the pinned binary on PATH}
  the committed plan in deploy/plan was rendered by ${pin}, and a different version renders a different
  plan, so this refuses instead of reporting drift.
  how to obtain it: docs/dependencies.md, section \"Obtaining the pinned schema tool\"; then either
    export PATH=\"<dir>:\$PATH\"    (the version is checked, not the directory)
    export CMSLEARN_PGSCHEMA=<path>"
}

cms_pgschema_resolve "${CMSLEARN_PGSCHEMA_VERSION:-}"
