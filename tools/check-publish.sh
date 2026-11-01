#!/usr/bin/env bash
# A draft becomes published only through machine evidence.
#
# The order of checks is the point: a task whose verification is pending is refused before its cases are even
# read, and a reference that does not reproduce its expected output stops the publish afterwards. The artifact
# is named by the digest of the published bytes, so publishing twice yields one path, not two.
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
generator="${1:-$repo/build/safety/judge/cms_generate_task}"
promoter="${2:-$repo/build/safety/judge/cms_promote_task}"
reference="${3:-$repo/build/safety/references/ref_range_sum_query_main}"

for tool in "$generator" "$promoter" "$reference"; do
  [ -x "$tool" ] || { echo "publish gate FAILED: $tool is not executable" >&2; exit 1; }
done

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
cp "$repo/content/tasks/range-sum-query/solutions/main.cpp" "$work/main.cpp"

"$generator" --out "$work/draft" --id probe-task --title-en "Probe" --title-th "ทดสอบ" \
  --topics "prefix-sums" --difficulty 900 --time-ms 1000 --memory-mb 256 --checker token \
  --statement-en "$work/statement.en" --statement-th "$work/statement.th" \
  --example-in "$work/example.in" --example-out "$work/example.out" --solution "$work/main.cpp" \
  >/dev/null 2>&1 || { echo "publish gate FAILED: the generator refused the specification" >&2; exit 1; }

# The plan's E7.2 falsification: verification is pending, so publication must be refused.
if "$promoter" "$work/draft" "$work/artifacts" "$reference" >/dev/null 2>&1; then
  bad "a task with pending verification is refused" "the promoter published it anyway"
else
  ok "a task with pending verification is refused"
fi
if [ -d "$work/artifacts" ] && [ -n "$(ls -A "$work/artifacts" 2>/dev/null)" ]; then
  bad "a refused publish writes no artifact" "$(ls "$work/artifacts")"
else
  ok "a refused publish writes no artifact"
fi

# With machine evidence recorded, the same task publishes and the artifact is named by its own digest.
sed -i 's/"status": "pending"/"status": "verified"/' "$work/draft/task.json"
if "$promoter" "$work/draft" "$work/artifacts" "$reference" >/dev/null 2>&1; then
  ok "a verified task is published"
else
  bad "a verified task is published" "the promoter refused machine evidence"
fi

digest="$(ls "$work/artifacts" 2>/dev/null | head -1)"
case "$digest" in
  [0-9a-f][0-9a-f]*) ok "the artifact is content-addressed" ;;
  *) bad "the artifact is content-addressed" "no digest-named file was written" ;;
esac
grep -q '"status": "published"' "$work/draft/task.json" \
  && ok "the manifest records the task as published" \
  || bad "the manifest records the task as published" "the status did not change"

# Publishing again must not rewrite an already published task.
if "$promoter" "$work/draft" "$work/artifacts" "$reference" >/dev/null 2>&1; then
  bad "an already published task is refused" "the promoter republished it"
else
  ok "an already published task is refused"
fi

# A reference that does not reproduce its expected output stops the publish, even with verification recorded.
"$generator" --out "$work/wrong" --id probe-wrong --title-en "Probe" --title-th "ทดสอบ" \
  --topics "prefix-sums" --difficulty 900 --time-ms 1000 --memory-mb 256 --checker token \
  --statement-en "$work/statement.en" --statement-th "$work/statement.th" \
  --example-in "$work/example.in" --example-out "$work/example.out" --solution "$work/main.cpp" \
  >/dev/null 2>&1
printf '999\n' >"$work/wrong/examples/1.out"
sed -i 's/"status": "pending"/"status": "verified"/' "$work/wrong/task.json"
if "$promoter" "$work/wrong" "$work/artifacts" "$reference" >/dev/null 2>&1; then
  bad "a reference that does not reproduce its output is refused" "the promoter published it"
else
  ok "a reference that does not reproduce its output is refused"
fi

printf '\n%s assertion(s) behaved, %s did not\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
