#!/usr/bin/env bash
# The live-database harness: every `[live]` case runs against a scratch cluster or the gate fails.
#
# The cases are tagged `[live]` in Catch2, not labelled in CTest, so the harness enumerates the tag from
# the built test binaries and then runs exactly those cases through ctest. Each test binary gets its own
# freshly-planned database, because a live case seeds fixed principals and bootstrap refuses a database
# that already holds one. A run that selects nothing is a failure, and a case that skips is named in the
# output rather than counted as a pass.
#
# Skips (exit 77) when the PostgreSQL server binaries are not installed.
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${1:-${CMS_LIVE_BUILD_DIR:-$repo/build/safety}}"
live_tag="live"

for tool in initdb pg_ctl psql ctest; do
  command -v "$tool" >/dev/null 2>&1 || { echo "  skipped: $tool is not installed"; exit 77; }
done
[ -d "$build" ] \
  || { echo "live gate FAILED: no build directory at $build; configure it with: cmake --preset safety" >&2; exit 1; }

work="$(mktemp -d)"
port=$((56600 + (RANDOM % 300)))
template_database=cmslearn_live_template

cleanup() {
  pg_ctl -D "$work/data" -m immediate stop >/dev/null 2>&1
  rm -rf "$work"
}
trap cleanup EXIT

pass=0
fail=0
ok() { printf '  [ok]   %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  [!!]   %s — %s\n' "$1" "$2"; fail=$((fail + 1)); }

dsn_for() { printf 'host=%s port=%s dbname=%s user=postgres' "$work" "$port" "$1"; }

initdb -D "$work/data" -U postgres -A trust >"$work/initdb.log" 2>&1 \
  || { echo "  [!!] initdb failed"; exit 1; }
pg_ctl -D "$work/data" -o "-c listen_addresses='' -c unix_socket_directories=$work -p $port" \
  -l "$work/pg.log" start >/dev/null 2>&1 || { echo "  [!!] pg_ctl failed"; exit 1; }
sleep 1
psql -X -h "$work" -p "$port" -U postgres -q -v ON_ERROR_STOP=1 -f "$repo/deploy/roles.sql" >/dev/null 2>&1 \
  || { echo "  [!!] deploy/roles.sql failed"; exit 1; }

# One planned template; each live binary starts from a copy so apply-plan runs once, not once per case.
psql -X -h "$work" -p "$port" -U postgres -q -c "CREATE DATABASE $template_database" >/dev/null 2>&1
PGHOST="$work" PGPORT="$port" PGDATABASE="$template_database" PGUSER=postgres \
  bash "$repo/tools/apply-plan.sh" >"$work/apply.log" 2>&1 \
  || { echo "  [!!] apply-plan failed"; tail -5 "$work/apply.log"; exit 1; }

escaped_regex() { printf '%s' "$1" | sed 's/[][\\^$.|?*+(){}]/\\&/g'; }

# The binaries own the tag; CTest does not know it, so the case list comes from the binaries themselves.
: >"$work/binaries"
binaries=0
selected=0
while IFS= read -r binary; do
  [ -n "$binary" ] || continue
  listing="$("$binary" "[$live_tag]" --list-tests 2>&1)"
  count="$(printf '%s\n' "$listing" | sed -n 's/^\([0-9][0-9]*\) matching test case[s]*$/\1/p')"
  if [ -z "$count" ]; then
    bad "the test binaries list their [$live_tag] cases" "$(basename "$binary"): $(printf '%s' "$listing" | tail -2)"
    continue
  fi
  [ "$count" -gt 0 ] || continue
  binaries=$((binaries + 1))
  : >"$work/cases.$binaries"
  printf '%s\n' "$listing" | sed -n 's/^  \([^ ].*\)$/\1/p' >"$work/cases.$binaries"
  printf '%s\n' "$binary" >"$work/binary.$binaries"
  while IFS= read -r name; do
    [ -n "$name" ] && selected=$((selected + 1))
  done <"$work/cases.$binaries"
done < <(find "$build" -maxdepth 4 -type f -name 'cms_*_tests' -perm -u+x | sort)

if [ "$selected" -eq 0 ]; then
  bad "the [$live_tag] tag selects at least one case" "selected 0 case(s) across $binaries binary/binaries"
else
  ok "the [$live_tag] tag selects $selected case(s) across $binaries binary/binaries"
fi

ran=0
skipped_names=()
live_database_number=0
index=0
while [ "$index" -lt "$binaries" ]; do
  index=$((index + 1))
  binary="$(cat "$work/binary.$index")"
  live_database_number=$((live_database_number + 1))
  case_database="${template_database}_case_${live_database_number}"
  psql -X -h "$work" -p "$port" -U postgres -q \
    -c "CREATE DATABASE $case_database TEMPLATE $template_database" >/dev/null 2>&1 \
    || { bad "each live binary gets a database" "$case_database could not be created"; continue; }

  regex=""
  count=0
  while IFS= read -r name; do
    [ -n "$name" ] || continue
    count=$((count + 1))
    regex="${regex:+$regex|}^$(escaped_regex "$name")\$"
  done <"$work/cases.$index"

  ctest_output="$(CMSLEAN_TEST_DSN="$(dsn_for "$case_database")" ctest --test-dir "$build" \
    -R "^($regex)$" --output-on-failure 2>&1)"
  status=$?

  counts="$(printf '%s\n' "$ctest_output" \
    | sed -n -e 's/.*tests passed, \([0-9][0-9]*\) tests failed out of \([0-9][0-9]*\).*/\1 \2/p' \
             -e 's/.*tests passed out of \([0-9][0-9]*\).*/0 \1/p')"
  failed_count="${counts% *}"
  matched="${counts#* }"
  if [ -z "$failed_count" ] || [ -z "$matched" ]; then
    bad "$(basename "$binary") runs its [$live_tag] cases" "$(printf '%s' "$ctest_output" | tail -4)"
    continue
  fi
  if [ "$matched" -ne "$count" ]; then
    bad "$(basename "$binary") runs its [$live_tag] cases" "ctest matched $matched of $count"
  fi

  while IFS= read -r line; do
    name="$(printf '%s' "$line" | sed -n 's/^[[:space:]]*[0-9][0-9]* - \(.*\) (Skipped)$/\1/p')"
    [ -n "$name" ] && skipped_names+=("$name")
  done < <(printf '%s\n' "$ctest_output" | awk '/The following tests did not run:/{show=1;next} show && /^$/{show=0} show')

  ran=$((ran + matched - 0))
  if [ "$status" -ne 0 ] || [ "$failed_count" -ne 0 ]; then
    bad "$(basename "$binary") passes its [$live_tag] cases" "$(printf '%s' "$ctest_output" | tail -6)"
  fi
done

if [ "$binaries" -gt 0 ]; then
  skipped_count="${#skipped_names[@]}"
  ran=$((selected - skipped_count))
  if [ "$ran" -gt 0 ]; then
    ok "the selected cases actually run ($ran of $selected ran, $skipped_count skipped)"
  else
    bad "the selected cases actually run" "0 of $selected ran; every selected case was skipped"
  fi
  for name in ${skipped_names[@]+"${skipped_names[@]}"}; do
    case "$name" in
      *[Ff]ork*|*[Ss]andbox*|*[Ii]solate*) printf '  [skip] legitimate: %s\n' "$name" ;;
      *) printf '  [skip] %s\n' "$name" ;;
    esac
  done
fi

printf '\nselected %s case(s), ran %s, skipped %s — %s assertion(s) behaved, %s did not\n' \
  "$selected" "$ran" "$skipped_count" "$pass" "$fail"
[ "$fail" -eq 0 ]
