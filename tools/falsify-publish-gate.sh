#!/usr/bin/env bash
# Proves the publish gate can fail.
#
# The gate exists to refuse a draft that has no machine evidence. A promoter that publishes anything must
# therefore fail it, and so must one that can never publish: if either passes, the gate is decorative.
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

pass=0
fail=0
ok() { printf '  [ok]   %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  [!!]   %s — %s\n' "$1" "$2"; fail=$((fail + 1)); }

gate="$repo/tools/check-publish.sh"
generator="$repo/build/safety/judge/cms_generate_task"
promoter="$repo/build/safety/judge/cms_promote_task"
reference="$repo/build/safety/references/ref_range_sum_query_main"

if bash "$gate" "$generator" "$promoter" "$reference" >/dev/null 2>&1; then
  ok "the real promoter passes the gate"
else
  bad "the real promoter passes the gate" "the gate refused the committed promoter"
fi

cat >"$work/always.sh" <<'FAKE'
#!/usr/bin/env bash
mkdir -p "$2"
printf 'published\n' >"$2/fake.json"
exit 0
FAKE
chmod +x "$work/always.sh"

if bash "$gate" "$generator" "$work/always.sh" "$reference" >/dev/null 2>&1; then
  bad "a promoter that publishes without evidence is refused" "the gate accepted it"
else
  ok "a promoter that publishes without evidence is refused"
fi

cat >"$work/never.sh" <<'FAKE'
#!/usr/bin/env bash
exit 1
FAKE
chmod +x "$work/never.sh"

if bash "$gate" "$generator" "$work/never.sh" "$reference" >/dev/null 2>&1; then
  bad "a promoter that can never publish is refused" "the gate accepted it"
else
  ok "a promoter that can never publish is refused"
fi

printf '\n%s assertion(s) behaved, %s did not\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
