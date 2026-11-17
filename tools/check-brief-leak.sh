#!/usr/bin/env bash
# The brief-leak gate: a title or a statement that repeats eight English words, or thirty-two Thai characters, of
# the teacher's request or of the four texts the chosen plan holds is refused before a student reads it.
#
# The rule lives in generator/src/statement_leak.cpp and the program is compiled from this tree on every run, the
# way check-visibility.sh rebuilds its serving test: a defect planted in the rule makes this gate go red instead of
# passing on a binary built before the plant. With a fixture directory as its only argument the gate compares that
# directory; with none it runs its own clean and leaky fixtures and asserts both answers (ADR 0035 §4).
#
# A fixture directory holds: brief.txt, description.txt, plan-approach.txt, plan-complexity.txt, plan.txt,
# plan-reason.txt, title.en.txt, title.th.txt, statement.en.txt, statement.th.txt.
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

die() { printf 'check:brief-leak FAILED: %s\n' "$*" >&2; exit 1; }
ok() { printf '  [ok]   %s\n' "$1"; }

cxx="${CXX:-g++}"
command -v "$cxx" >/dev/null 2>&1 || die "no C++ compiler at $cxx"

cat >"$work/main.cpp" <<'CPP'
#include <cms/generate/statement.hpp>

int main(int argc, char** argv) {
  return cms::generate::leak_command(argc, argv);
}
CPP

"$cxx" -std=c++26 -O1 -Wall -Wextra -Werror -I"$repo/core/include" -I"$repo/generator/include" \
  -I"$repo/generator/src" "$work/main.cpp" "$repo/generator/src/statement_leak.cpp" -o "$work/leak" \
  || die "the leak rule does not compile"

compare() { # <fixture directory>
  local directory="$1" part locale kind
  local parts=(brief description plan-approach plan-complexity plan plan-reason)
  for part in "${parts[@]}"; do
    [ -f "$directory/$part.txt" ] || die "the fixture $directory holds no $part.txt"
  done
  for locale in en th; do
    for kind in title statement; do
      [ -f "$directory/$kind.$locale.txt" ] || die "the fixture $directory holds no $kind.$locale.txt"
    done
  done
  "$work/leak" brief \
    "$directory/brief.txt" "$directory/description.txt" \
    "$directory/plan-approach.txt" "$directory/plan-complexity.txt" \
    "$directory/plan.txt" "$directory/plan-reason.txt" \
    "$directory/title.en.txt" "$directory/title.th.txt" \
    "$directory/statement.en.txt" "$directory/statement.th.txt"
}

expect_clean() { # <fixture directory>
  local output status
  output="$(compare "$1" 2>&1)" && status=0 || status=$?
  if [ "$status" -ne 0 ]; then
    printf '%s\n' "$output" >&2
    die "$1 repeats the request, but the fixture is the clean control"
  fi
  ok "$1 repeats nothing from the request or the plan"
}

expect_refused() { # <fixture directory> <what the fixture breaks>
  local output status
  output="$(compare "$1" 2>&1)" && status=0 || status=$?
  if [ "$status" -eq 0 ]; then
    die "$1 repeats the request and was accepted ($2)"
  fi
  printf '%s\n' "$output" | grep -q 'repeats' \
    || die "$1 was refused without naming what it repeats: $output"
  ok "$2 is refused: $(printf '%s' "$output" | head -1)"
}

if [ "$#" -gt 0 ]; then
  [ -d "$1" ] || die "no fixture directory at $1"
  output="$(compare "$1" 2>&1)" && status=0 || status=$?
  if [ "$status" -ne 0 ]; then
    printf '%s\n' "$output" >&2
    die "$1 repeats the request or the plan"
  fi
  ok "$1 repeats nothing from the request or the plan"
  printf '\nthe fixture repeats nothing\n'
  exit 0
fi

clean="$work/clean"
mkdir -p "$clean"
printf '%s\n' "An array task with questions about ranges of positions." >"$clean/brief.txt"
printf '%s\n' "Range sums." >"$clean/description.txt"
printf '%s\n' "prefix sums" >"$clean/plan-approach.txt"
printf '%s\n' "O(n)" >"$clean/plan-complexity.txt"
printf '%s\n' '{"approach":"prefix sums","n_max":200000}' >"$clean/plan.txt"
printf '%s\n' "the plan fits the declared bounds" >"$clean/plan-reason.txt"
printf '%s\n' "Range Sum Queries" >"$clean/title.en.txt"
printf '%s\n' "ผลรวมของช่วงข้อมูล" >"$clean/title.th.txt"
printf '%s\n' "# Range Sum Queries" "" "You are given n integers and a list of ranges; print the sum of each." \
  >"$clean/statement.en.txt"
printf '%s\n' "# ผลรวมของช่วงข้อมูล" "" "กำหนดจำนวนเต็ม n จำนวน แล้วพิมพ์ผลรวมของช่วงที่ถาม" >"$clean/statement.th.txt"

expect_clean "$clean"

leaked_title="$work/leaked-title"
cp -R "$clean" "$leaked_title"
printf '%s\n' "An array task with questions about ranges of positions" >"$leaked_title/title.en.txt"
expect_refused "$leaked_title" "a title copied from the request"

leaked_statement="$work/leaked-statement"
cp -R "$clean" "$leaked_statement"
printf '%s\n' "# Range Sum Queries" "" \
  "An array task with questions about ranges of positions is described below, with the sums to print." \
  >"$leaked_statement/statement.en.txt"
expect_refused "$leaked_statement" "a statement copied from the request"

leaked_thai="$work/leaked-thai"
cp -R "$clean" "$leaked_thai"
printf '%s\n' "กำหนดอาร์เรย์ของจำนวนเต็มแล้วตอบคำถามเกี่ยวกับผลรวมของช่วงที่กำหนดให้ทุกคำถาม" \
  >"$leaked_thai/description.txt"
printf '%s\n' "# ผลรวมของช่วงข้อมูล" "" \
  "กำหนดอาร์เรย์ของจำนวนเต็มแล้วตอบคำถามเกี่ยวกับผลรวมของช่วงที่กำหนดให้ทุกคำถาม" >"$leaked_thai/statement.th.txt"
expect_refused "$leaked_thai" "a Thai statement copied from the request"

printf '\nthe clean control passes, and a copied title, a copied statement and a copied Thai sentence are refused\n'
