#!/usr/bin/env bash
# The token checker, run the way the judge runs it: (input, output, answer) as three paths, verdict on
# standard error, exit status 0 whatever the verdict.
#
# This is the only test that compiles and executes the vendored testlib. If the header is the wrong
# version, or the judge's parser and testlib's vocabulary have drifted apart, it fails here.
set -uo pipefail

checker="${1:?usage: token_checker_test.sh <path to cms_checker_token>}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

pass=0
fail=0
ok() { printf '  [ok]   %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  [!!]   %s — %s\n' "$1" "$2"; fail=$((fail + 1)); }

printf '3\n1 2 3\n' >"$work/input"
printf '3\n1 2 3\n' >"$work/answer"

run() {
  local output="$1"
  printf '%s' "$output" >"$work/output"
  "$checker" "$work/input" "$work/output" "$work/answer" 2>&1
}

verdict="$(run '3
1 2 3
')"
status=$?
if [ "$status" -eq 0 ] && [[ "$verdict" == ok* ]]; then
  ok "a matching output is accepted"
else
  bad "a matching output is accepted" "status $status, said: $verdict"
fi

verdict="$(run '3
1 2 4
')"
if [[ "$verdict" == "wrong answer"* ]]; then
  ok "a differing token is a wrong answer"
else
  bad "a differing token is a wrong answer" "said: $verdict"
fi

verdict="$(run '3
1 2
')"
if [[ "$verdict" == "wrong answer"* ]]; then
  ok "an output missing tokens is a wrong answer"
else
  bad "an output missing tokens is a wrong answer" "said: $verdict"
fi

verdict="$(run '3
1 2 3
4
')"
if [[ "$verdict" == "wrong output format"* ]]; then
  ok "surplus tokens are a format error"
else
  bad "surplus tokens are a format error" "said: $verdict"
fi

# The plan's E5.1 falsification at the checker boundary: a malformed verdict must not be read as an
# acceptance. The stub says "pass", which is not testlib's vocabulary — the same stub the parser case in
# verdict_test.cpp feeds to the judge, so the refusal is asserted on the judge's own code path.
malformed="$("${BASH_SOURCE[0]%/*}"/stubs/garbage-checker.sh 2>&1)"
if [[ "$malformed" != ok* ]]; then
  ok "a malformed verdict is not an acceptance"
else
  bad "a malformed verdict is not an acceptance" "it read as ok"
fi

printf '\n%s assertion(s) behaved, %s did not\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
