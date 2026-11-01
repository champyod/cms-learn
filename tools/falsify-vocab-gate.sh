#!/usr/bin/env bash
# Proves the vocabulary gate can fail.
#
# The gate exists for the failures nothing else notices: a syllabus statement nobody mapped, an alias
# that means two things, a parent chain that loops, a Thai name with no provenance, a stored id that
# stopped resolving. Each case plants one of those in a throwaway copy and requires the line that names
# it — the gate exits non-zero on every violation at once, so a non-zero exit alone would not say which
# one it found.
#
# Usage: bash tools/falsify-vocab-gate.sh
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
export TMPDIR="$work"

for tool in bun python3; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    printf 'cannot be exercised: %s is not on PATH\n' "$tool" >&2
    exit 77
  fi
done

pass=0
fail=0

stage() {
  local dir="$1"
  rm -rf "$dir"
  mkdir -p "$dir/content" "$dir/tools"
  cp -a "$repo/seed" "$dir/seed"
  cp -a "$repo/tools/vocab" "$dir/tools/vocab"
  cp "$repo/content/topics.json" "$repo/content/tracks.json" "$dir/content/"
}

# case_of <name> <expected substring> <command> <mutation run inside the staged root>
case_of() {
  local name="$1" expect="$2" command="$3" mutate="$4"
  local dir="$work/$name" before after out rc
  stage "$dir"
  before="$(cd "$dir" && find . -type f -exec sha256sum {} + | sort | sha256sum)"
  ( cd "$dir" && eval "$mutate" ) || { printf 'SETUP FAILED  %s\n' "$name"; fail=$((fail + 1)); return; }
  after="$(cd "$dir" && find . -type f -exec sha256sum {} + | sort | sha256sum)"
  if [ "$before" = "$after" ]; then
    printf 'SETUP FAILED  %s — mutation changed nothing (stale pattern?)\n' "$name"
    fail=$((fail + 1))
    return
  fi
  out="$( cd "$dir" && eval "$command" 2>&1 )"
  rc=$?
  if [ "$rc" -eq 0 ]; then
    printf 'NOT CAUGHT    %s — the gate passed a broken vocabulary\n' "$name"
    fail=$((fail + 1))
  elif [[ "$out" != *"$expect"* ]]; then
    printf 'WRONG MESSAGE %s — expected "%s", got:\n%s\n' "$name" "$expect" "$out"
    fail=$((fail + 1))
  else
    printf 'caught        %s\n' "$name"
    pass=$((pass + 1))
  fi
}

# Each mutation edits the staged copy in place; python3 keeps the JSON valid so the gate reaches the
# check under test instead of failing to parse the file.
plant_parent_cycle() {
  python3 - <<'PY'
import json

path = "seed/vocab/vocabulary.json"
with open(path) as handle:
    vocabulary = json.load(handle)
child = next(node for node in vocabulary["nodes"] if node["parent"] is not None)
for node in vocabulary["nodes"]:
    if node["id"] == child["parent"]:
        node["parent"] = child["id"]
with open(path, "w") as handle:
    json.dump(vocabulary, handle, indent=2, ensure_ascii=False)
PY
}

plant_shared_alias() {
  python3 - <<'PY'
import json

path = "seed/vocab/vocabulary.json"
with open(path) as handle:
    vocabulary = json.load(handle)
donor, receiver = vocabulary["nodes"][0], vocabulary["nodes"][1]
receiver["aliases"].append(dict(donor["aliases"][0]))
with open(path, "w") as handle:
    json.dump(vocabulary, handle, indent=2, ensure_ascii=False)
PY
}

plant_unmapped_statement() {
  python3 - <<'PY'
import json

path = "seed/vocab/mapping.json"
with open(path) as handle:
    mapping = json.load(handle)
mapping["mapped"].pop(next(iter(mapping["mapped"])))
with open(path, "w") as handle:
    json.dump(mapping, handle, indent=2, ensure_ascii=False)
PY
}

plant_name_without_provenance() {
  python3 - <<'PY'
import json

path = "seed/vocab/vocabulary.json"
with open(path) as handle:
    vocabulary = json.load(handle)
node = next(node for node in vocabulary["nodes"] if node["th"] is not None)
node["th_source"] = None
with open(path, "w") as handle:
    json.dump(vocabulary, handle, indent=2, ensure_ascii=False)
PY
}

plant_stale_topic_id() {
  python3 - <<'PY'
import json

path = "content/topics.json"
with open(path) as handle:
    document = json.load(handle)
document["topics"][0]["id"] = "no-such-topic"
with open(path, "w") as handle:
    json.dump(document, handle, indent=2, ensure_ascii=False)
PY
}

plant_unknown_stage_tag() {
  python3 - <<'PY'
import json

path = "content/tracks.json"
with open(path) as handle:
    tracks = json.load(handle)
tracks["tracks"][0]["stages"][0]["tags"].append("no-such-tag")
with open(path, "w") as handle:
    json.dump(tracks, handle, indent=2, ensure_ascii=False)
PY
}

# Control: an untouched copy must pass, or every case below is meaningless.
control="$work/control"
stage "$control"
if ( cd "$control" && bun run tools/vocab/gate.ts >/dev/null 2>&1 ); then
  printf 'behaved       control (an unmodified tree passes)\n'
  pass=$((pass + 1))
else
  printf 'BROKEN        control — the gate rejects a valid tree\n'
  fail=$((fail + 1))
fi

case_of "parent-cycle" "parent cycle" "bun run tools/vocab/gate.ts" plant_parent_cycle
case_of "alias-on-two-nodes" "is on both" "bun run tools/vocab/gate.ts" plant_shared_alias
case_of "statement-neither-mapped-nor-dropped" "is neither mapped nor dropped" \
  "bun run tools/vocab/gate.ts" plant_unmapped_statement
case_of "translation-without-provenance" "has no provenance" \
  "bun run tools/vocab/gate.ts" plant_name_without_provenance
case_of "topic-id-no-longer-resolves" 'id "no-such-topic" no longer resolves' \
  "bun run tools/vocab/gate.ts" plant_stale_topic_id
case_of "stage-tag-no-longer-resolves" 'tag "no-such-tag" does not resolve' \
  "bun run tools/vocab/gate.ts" plant_unknown_stage_tag

printf '\n%s case(s) behaved, %s did not\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
