#!/usr/bin/env bash
# The fast tier: what a change must pass, and nothing that belongs to the nightly proof.
#
# The project has two tiers. A change runs this one: the schema, the content, the build, the unit,
# live and end-to-end cases, and the fast gates over what they produced. The full proof — every
# falsification, the clang-tidy analysis, the deploy contract, the security scan, sandbox isolation —
# is `bun run gate:all`, which still contains all of it and runs nightly. This script never deletes a
# gate; it runs the fast subset and says on its last line what it left for the full tier.
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo" || exit 1

tier_notice() {
  printf '\ngate:fast did not run: every falsification, clang-tidy static analysis, the deploy contract and upgrade, the security scan, sandbox isolation — the full proof is `bun run gate:all`.\n'
}
trap tier_notice EXIT

step() {
  local name="$1"
  shift
  printf '\n── %s\n' "$name"
  if ! "$@"; then
    printf 'gate:fast FAILED: %s\n' "$name" >&2
    exit 1
  fi
}

# Everything that reads the tree without a build: cheap, and it fails a change before the compiler runs.
step "schema" bun run check:schema
step "content" bun run validate
step "vocabulary" bash -c 'bun run vocab:build && bun run vocab:gate'
step "references" bun run check:references

# The build and every case that runs against it, without the static analysis the full tier adds.
step "cpp build, unit, live and end-to-end cases" bun run check:cpp:fast

# The cases that need the binaries the step above built, or only a scratch cluster.
step "database" bun run check:db
step "content database" bun run check:content:db
step "query plan" bun run check:unseen
step "visibility" bun run check:visibility
step "verifier" bun run check:verify
step "judge" bun run check:judge
step "render" bun run check:render
step "live database cases" bun run check:live

# The remaining fast gates: templates, settings, the generation barrier, and the workspace itself.
step "templates" bun run check:templates
step "editor island" bun run check:island
step "settings" bun run check:settings
step "catalogue" bun run check:catalogue
step "generator" bun run check:generator
step "publish" bun run check:publish
step "generation barrier" bun run check:pipeline
step "workspace scaffold" bun run check:scaffold
step "control client" bun run check:cli
step "package manager" bun run check:pm
step "stack template" bun run check:stack
