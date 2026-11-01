#!/usr/bin/env bash
# A generated draft must be the shape the loader reads, and two runs must be identical.
#
# The generator is a pure function of its arguments: same command line, same bytes. That is what makes a draft
# reviewable as a diff, so it is asserted here with `diff -r` rather than assumed from the absence of a clock.
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
generator="${1:-$repo/build/safety/judge/cms_generate_task}"

[ -x "$generator" ] || { echo "generator gate FAILED: no generator at $generator" >&2; exit 1; }

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

pass=0
fail=0
ok() { printf '  [ok]   %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  [!!]   %s — %s\n' "$1" "$2"; fail=$((fail + 1)); }

printf '3 2\n1 2 3\n1 2\n2 3\n' >"$work/example.in"
printf '3\n5\n' >"$work/example.out"
printf 'ให้ลำดับตัวเลขและคำถาม\n' >"$work/statement.th"
printf 'Given a sequence and queries\n' >"$work/statement.en"
printf '#include <iostream>\nint main() { return 0; }\n' >"$work/main.cpp"

generate() {
  "$generator" --out "$1" --id probe-task --title-en "Probe" --title-th "ทดสอบ" \
    --topics "prefix-sums,complexity" --difficulty 900 --time-ms 1000 --memory-mb 256 \
    --checker token --statement-en "$work/statement.en" --statement-th "$work/statement.th" \
    --example-in "$work/example.in" --example-out "$work/example.out" --solution "$work/main.cpp"
}

if generate "$work/one" >/dev/null 2>&1; then ok "a complete specification is written"; \
  else bad "a complete specification is written" "the generator refused it"; fi

for file in task.json statement.en.md statement.th.md examples/1.in examples/1.out solutions/main.cpp; do
  [ -f "$work/one/$file" ] || bad "the draft holds $file" "missing"
done
[ -f "$work/one/task.json" ] && ok "the draft holds every file the loader reads"

missing_field=0
for field in '"id"' '"version"' '"status"' '"title"' '"topics"' '"difficulty"' '"locales"' '"limits"' '"checker"' '"origin"' '"verification"'; do
  grep -q "$field" "$work/one/task.json" || { missing_field=1; echo "    missing $field"; }
done
[ "$missing_field" = "0" ] && ok "the manifest carries every field the schema requires" \
  || bad "the manifest carries every field the schema requires" "see above"

generate "$work/two" >/dev/null 2>&1
if diff -r "$work/one" "$work/two" >/dev/null 2>&1; then ok "two runs produce identical bytes"; \
  else bad "two runs produce identical bytes" "$(diff -r "$work/one" "$work/two" | head -3)"; fi

if generate "$work/one" >/dev/null 2>&1; then
  bad "an existing directory is not overwritten" "the generator wrote over a draft"
else
  ok "an existing directory is not overwritten"
fi

if "$generator" --out "$work/incomplete" --id probe-task --title-en "Probe" >/dev/null 2>&1; then
  bad "an incomplete specification is refused" "the generator wrote a half task"
else
  ok "an incomplete specification is refused"
fi

if ( cd "$repo" && bun run validate >/dev/null 2>&1 ); then
  ok "the repository bank still passes the content gate"
else
  bad "the repository bank still passes the content gate" "bun run validate failed"
fi

printf '\n%s assertion(s) behaved, %s did not\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
