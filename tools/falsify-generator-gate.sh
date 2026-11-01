#!/usr/bin/env bash
# Proves the generator gate can fail.
#
# The gate's value is that it refuses a draft the loader could not read. A fake generator that writes a
# manifest with a field missing must therefore fail the gate — if the gate passes it, the gate is decorative.
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

pass=0
fail=0
ok() { printf '  [ok]   %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  [!!]   %s — %s\n' "$1" "$2"; fail=$((fail + 1)); }

gate="$repo/tools/check-generator.sh"
generator="$repo/build/safety/judge/cms_generate_task"

if bash "$gate" "$generator" >/dev/null 2>&1; then
  ok "the real generator passes the gate"
else
  bad "the real generator passes the gate" "the gate refused the committed generator"
fi

# A generator that omits a required field. The gate must notice, because the loader would not.
cat >"$work/missing-field.sh" <<'FAKE'
#!/usr/bin/env bash
out=""
while [ "$#" -gt 0 ]; do [ "$1" = "--out" ] && { out="$2"; shift; }; shift; done
mkdir -p "$out/examples" "$out/solutions"
printf '{\n  "id": "probe-task",\n  "version": 1,\n  "status": "draft"\n}\n' >"$out/task.json"
printf 'x\n' >"$out/statement.en.md"
printf 'x\n' >"$out/statement.th.md"
printf 'x\n' >"$out/examples/1.in"
printf 'x\n' >"$out/examples/1.out"
printf 'x\n' >"$out/solutions/main.cpp"
FAKE
chmod +x "$work/missing-field.sh"

if bash "$gate" "$work/missing-field.sh" >/dev/null 2>&1; then
  bad "a manifest with a field missing is refused" "the gate accepted an unreadable draft"
else
  ok "a manifest with a field missing is refused"
fi

# A generator whose output is not reproducible: the gate compares two runs and must see the difference.
cat >"$work/unstable.sh" <<'FAKE'
#!/usr/bin/env bash
out=""
while [ "$#" -gt 0 ]; do [ "$1" = "--out" ] && { out="$2"; shift; }; shift; done
mkdir -p "$out/examples" "$out/solutions"
printf '{"id": "probe-task", "version": 1, "status": "draft", "title": {}, "topics": [], "difficulty": 900, "locales": [], "limits": {}, "checker": {}, "origin": {}, "verification": {}, "nonce": "%s"}\n' "$RANDOM" >"$out/task.json"
printf 'x\n' >"$out/statement.en.md"
printf 'x\n' >"$out/statement.th.md"
printf 'x\n' >"$out/examples/1.in"
printf 'x\n' >"$out/examples/1.out"
printf 'x\n' >"$out/solutions/main.cpp"
FAKE
chmod +x "$work/unstable.sh"

if bash "$gate" "$work/unstable.sh" >/dev/null 2>&1; then
  bad "output that differs between runs is refused" "the gate accepted a non-deterministic draft"
else
  ok "output that differs between runs is refused"
fi

printf '\n%s assertion(s) behaved, %s did not\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
