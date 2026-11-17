#!/usr/bin/env bash
# The algorithm-leak gate: a title or a statement that names the intended approach is refused, whether or not
# misdirection is enabled (ADR 0031 §2, ADR 0036 §3). A technique is matchable by its topic id, its English
# label, its Thai label and the acronym of that label, so "Dijkstra", "Shortest paths" and the Thai label are one
# rule rather than three spellings a writer can dodge.
#
# The rule lives in generator/src/statement_leak.cpp and the program is compiled from this tree on every run, so a
# defect planted in the rule makes this gate go red instead of passing on an older binary.
#
# A fixture directory holds: technique.txt (the topic id), title.en.txt, title.th.txt, statement.en.txt,
# statement.th.txt. The labels come from content/topics.json, the one place a topic id is defined.
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

die() { printf 'check:algo-leak FAILED: %s\n' "$*" >&2; exit 1; }
ok() { printf '  [ok]   %s\n' "$1"; }

cxx="${CXX:-g++}"
command -v "$cxx" >/dev/null 2>&1 || die "no C++ compiler at $cxx"
command -v bun >/dev/null 2>&1 || die "bun is not installed, so the vocabulary cannot be read"

cat >"$work/main.cpp" <<'CPP'
#include <cms/generate/statement.hpp>

int main(int argc, char** argv) {
  return cms::generate::leak_command(argc, argv);
}
CPP

"$cxx" -std=c++26 -O1 -Wall -Wextra -Werror -I"$repo/core/include" -I"$repo/generator/include" \
  -I"$repo/generator/src" "$work/main.cpp" "$repo/generator/src/statement_leak.cpp" -o "$work/leak" \
  || die "the leak rule does not compile"

names_of() { # <topic id>
  ( cd "$repo" && bun -e '
      const id = process.argv[1];
      const vocabulary = JSON.parse(require("node:fs").readFileSync("content/topics.json", "utf8"));
      const topic = vocabulary.topics.find((entry) => entry.id === id);
      if (topic === undefined) process.exit(3);
      process.stdout.write([topic.id, topic.en, topic.th].join("\n"));
    ' "$1" )
}

compare() { # <fixture directory>
  local directory="$1" locale kind
  [ -f "$directory/technique.txt" ] || die "the fixture $directory holds no technique.txt"
  local id
  id="$(sed -n '1p' "$directory/technique.txt")"
  [ -n "$id" ] || die "the fixture $directory names no technique"
  local resolved
  resolved="$(names_of "$id")" || die "content/topics.json does not define the technique \"$id\""
  local fields=()
  while IFS= read -r line; do fields+=("$line"); done <<<"$resolved"
  for locale in en th; do
    for kind in title statement; do
      [ -f "$directory/$kind.$locale.txt" ] || die "the fixture $directory holds no $kind.$locale.txt"
    done
  done
  "$work/leak" algo "${fields[0]}" "${fields[1]}" "${fields[2]}" - \
    "$directory/title.en.txt" "$directory/title.th.txt" \
    "$directory/statement.en.txt" "$directory/statement.th.txt"
}

expect_clean() { # <fixture directory>
  local output status
  output="$(compare "$1" 2>&1)" && status=0 || status=$?
  if [ "$status" -ne 0 ]; then
    printf '%s\n' "$output" >&2
    die "$1 names the approach, but the fixture is the clean control"
  fi
  ok "$1 names no intended approach"
}

expect_refused() { # <fixture directory> <what the fixture breaks>
  local output status
  output="$(compare "$1" 2>&1)" && status=0 || status=$?
  if [ "$status" -eq 0 ]; then
    die "$1 names the approach and was accepted ($2)"
  fi
  printf '%s\n' "$output" | grep -q 'names the intended approach' \
    || die "$1 was refused without naming what it names: $output"
  ok "$2 is refused: $(printf '%s' "$output" | head -1)"
}

if [ "$#" -gt 0 ]; then
  [ -d "$1" ] || die "no fixture directory at $1"
  output="$(compare "$1" 2>&1)" && status=0 || status=$?
  if [ "$status" -ne 0 ]; then
    printf '%s\n' "$output" >&2
    die "$1 names the intended approach"
  fi
  ok "$1 names no intended approach"
  printf '\nthe fixture names no intended approach\n'
  exit 0
fi

clean="$work/clean"
mkdir -p "$clean"
printf '%s\n' "dp" >"$clean/technique.txt"
printf '%s\n' "Counting the ways" >"$clean/title.en.txt"
printf '%s\n' "การนับจำนวนวิธี" >"$clean/title.th.txt"
printf '%s\n' "# Counting the ways" "" "You are given n and must print the number of ways." >"$clean/statement.en.txt"
printf '%s\n' "# การนับจำนวนวิธี" "" "กำหนดค่า n แล้วพิมพ์จำนวนวิธีทั้งหมด" >"$clean/statement.th.txt"

expect_clean "$clean"

leaked_title="$work/leaked-title"
cp -R "$clean" "$leaked_title"
printf '%s\n' "Dynamic Programming on a Grid" >"$leaked_title/title.en.txt"
expect_refused "$leaked_title" "the English label in a title"

leaked_id="$work/leaked-id"
cp -R "$clean" "$leaked_id"
printf '%s\n' "# Counting the ways" "" "Build a dp table over the values and add it up." >"$leaked_id/statement.en.txt"
expect_refused "$leaked_id" "the topic id in a statement"

leaked_thai="$work/leaked-thai"
cp -R "$clean" "$leaked_thai"
printf '%s\n' "การนับจำนวนวิธี" "" "ใช้การโปรแกรมพลวัตกับค่าที่กำหนด" >"$leaked_thai/statement.th.txt"
expect_refused "$leaked_thai" "the Thai label in a statement"

printf '\nthe clean control passes, and the label, the id and the Thai label are refused\n'
