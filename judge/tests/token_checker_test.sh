#!/usr/bin/env bash
# The token checker, run the way the judge runs it: (input, output, answer) as three paths, verdict on
# standard error, exit status 0 whatever the verdict.
#
# This is the only test that compiles and executes the vendored testlib. If the header is the wrong
# version, or the judge's parser and testlib's vocabulary have drifted apart, it fails here.
#
# The second half is the cross-check ADR 0042 asks for: the judge's checker and the pipeline's own token
# rule (core/include/cms/tokens.hpp, which the differential calls) must give the same verdict on the same
# pair of answers. The probe is compiled here from that header, so the two rules cannot drift apart
# without this failing.
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

# The pipeline's rule, compiled from the same header the differential includes.
repo="$(cd "${BASH_SOURCE[0]%/*}/../.." && pwd)"
cat >"$work/probe.cpp" <<'PROBE'
#include <cms/tokens.hpp>

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

auto read(const char* path) -> std::string {
  auto stream = std::ifstream{path, std::ios::binary};
  return std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

int main(int argc, char** argv) {
  if (argc != 3) return 2;
  std::cout << (cms::tokens_equal(read(argv[1]), read(argv[2])) ? "agree" : "differ") << "\n";
  return 0;
}
PROBE
if ! c++ -std=c++20 -I"$repo/core/include" -o "$work/probe" "$work/probe.cpp" >"$work/probe.log" 2>&1; then
  bad "the pipeline's token rule compiles" "$(head -1 "$work/probe.log")"
else
  ok "the pipeline's token rule compiles"

  # expected answer, contestant answer, what both rules must say
  cross_check() {
    local expected="$1" answer="$2" wanted="$3"
    printf '%s' "$expected" >"$work/expected"
    printf '%s' "$answer" >"$work/candidate"
    local said
    said="$("$work/probe" "$work/expected" "$work/candidate")"
    printf '%s' "$answer" >"$work/output"
    local verdict_text
    verdict_text="$("$checker" "$work/input" "$work/output" "$work/answer" 2>&1)"
    local checker_says="differ"
    [[ "$verdict_text" == ok* ]] && checker_says="agree"
    if [ "$said" = "$wanted" ] && [ "$checker_says" = "$wanted" ]; then
      ok "both rules $wanted: $4"
    else
      bad "both rules $wanted: $4" "the probe said $said, the checker said $checker_says ($verdict_text)"
    fi
  }

  cross_check '3
1 2 3
' '3
1 2 3
' agree "the same tokens"
  cross_check '3
1 2 3
' '3
1 2 3

' agree "trailing whitespace"
  cross_check '3
1 2 3
' '3   1 2 3
' agree "three spaces where one was"
  cross_check '3
1 2 3
' '3
1
2
3
' agree "a line break between values"
  cross_check '3
1 2 3
' '3
1 2
' differ "a missing token"
  cross_check '3
1 2 3
' '3
1 2 3
4
' differ "an extra token"
  cross_check '3
1 2 3
' '3
1 2 4
' differ "a changed token"
fi

printf '\n%s assertion(s) behaved, %s did not\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
