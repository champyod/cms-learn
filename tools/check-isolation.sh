#!/usr/bin/env bash
# A fork bomb must be contained by the sandbox and the box torn down afterwards.
#
# This runs in the container that ships the judge, not on the host: isolate creates namespaces and cgroups,
# which is a privileged operation, and measuring it anywhere else would prove something about that machine
# rather than about the judge. The C++ supervisor tests cover the argument list, the deadline and the
# escalation with stubs; this covers the containment claim itself.
#
# The probe has two outcomes that must never be confused: the sandbox stopped the program (status SG or TO),
# or the sandbox did not run it at all (status RE, XX, or no status). The second is inconclusive, and this
# gate fails on it: a sandbox that cannot execute the probe proves nothing about containment. The probe is a
# static binary copied into the sandbox's own `box/` directory — isolate runs programs from there, and a copy
# beside it is a file the sandbox never sees, which is exactly how RE was being read as containment.
#
# Skips (exit 77) when Docker is not available, the same way the database gates skip without PostgreSQL.
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
isolation_dir="${ISOLATION_DIR:-$repo/judge/isolation}"
image="cms-learn-isolate:check"

command -v docker >/dev/null 2>&1 || { echo "  skipped: docker is not installed"; exit 77; }
docker info >/dev/null 2>&1 || { echo "  skipped: no docker daemon is reachable"; exit 77; }

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

if ! docker build --tag "$image" "$isolation_dir" >"$work/build.log" 2>&1; then
  echo "isolation gate FAILED: the sandbox image did not build" >&2
  tail -5 "$work/build.log" >&2
  exit 1
fi

# A shell script cannot be the probe: the sandbox's box has no /bin/sh unless it is bound in, and the gate is
# about what the sandbox does with a program, not about which interpreter it carries. A static binary needs
# nothing from the box.
command -v gcc >/dev/null 2>&1 || {
  echo "isolation gate FAILED: gcc is not installed, so the probe cannot be built" >&2
  exit 1
}
if ! gcc -static -O2 -o "$work/bomb" -xc - >"$work/gcc.log" 2>&1 <<'BOMB'
#include <unistd.h>
int main(void) {
    for (;;) {
        if (fork() < 0) {
            sleep(1);
        }
    }
    return 0;
}
BOMB
then
  echo "isolation gate FAILED: the probe did not compile as a static binary" >&2
  tail -5 "$work/gcc.log" >&2
  exit 1
fi

read -r -d '' probe <<'PROBE'
set -u
box="$(isolate --init --box-id 0)" || {
  echo "verdict=inconclusive reason=the sandbox could not initialize (isolate --init failed)"
  exit 3
}
# isolate runs the program from <box>/box, the root the sandbox sees; a file beside it is invisible.
if ! cp /bomb "$box/box/bomb" 2>/dev/null; then
  echo "verdict=inconclusive reason=the sandbox root is missing, so the probe cannot be placed"
  isolate --cleanup --box-id 0 >/dev/null 2>&1
  exit 3
fi
chmod +x "$box/box/bomb"
status=0
isolate --box-id 0 --time 1 --wall-time 3 --mem 131072 \
  --stdout out.txt --stderr err.txt --meta "$box/meta.txt" --run -- ./bomb || status=$?
verdict="$(sed -n 's/^status:\(.*\)$/\1/p' "$box/meta.txt" 2>/dev/null)"
message="$(sed -n 's/^message:\(.*\)$/\1/p' "$box/meta.txt" 2>/dev/null)"
isolate --cleanup --box-id 0 >/dev/null 2>&1
if [ -d "$box" ]; then
  echo "verdict=inconclusive reason=the box was not torn down"
  exit 9
fi
case "$verdict" in
  SG) echo "verdict=contained reason=the sandbox killed the program (status SG, isolate exit $status)" ;;
  TO) echo "verdict=contained reason=the sandbox stopped the program at the time limit (status TO)" ;;
  "") echo "verdict=inconclusive reason=no status was written (isolate exit $status${message:+, ${message}})" ;;
  RE) echo "verdict=inconclusive reason=the program did not execute (status RE, isolate exit $status${message:+, ${message}})" ;;
  XX) echo "verdict=inconclusive reason=isolate reported an internal error (status XX${message:+, ${message}})" ;;
  *) echo "verdict=inconclusive reason=unexpected status $verdict (isolate exit $status)" ;;
esac
case "$verdict" in
  SG|TO) exit 0 ;;
  *) exit 3 ;;
esac
PROBE

# The pids limit is the container's own backstop: the gate runs without cgroups inside (isolate's process
# limit needs a delegated cgroup tree), so an unbounded fork bomb would otherwise be bounded only by the
# time limit. The sandbox still starts and stops the program, which is what this gate is about.
output="$(docker run --rm --privileged --pids-limit 256 --entrypoint bash -v "$work/bomb:/bomb:ro" "$image" -c "$probe" 2>&1)"
code=$?

verdict_line="$(printf '%s\n' "$output" | sed -n 's/^verdict=\(contained\|inconclusive\) reason=//p' | tail -1)"
if [ "$code" -eq 0 ] && [ -n "$verdict_line" ]; then
  printf '  [ok]   the fork bomb was contained (%s)\n' "$verdict_line"
  printf 'isolation gate: OK\n'
  exit 0
fi
if [ "$code" -eq 3 ]; then
  printf 'isolation gate FAILED: inconclusive — %s\n' "${verdict_line:-the probe produced no verdict}" >&2
  printf '%s\n' "$output" >&2
  exit 1
fi
printf 'isolation gate FAILED: the sandbox did not contain the bomb (exit %s)\n' "$code" >&2
printf '%s\n' "$output" >&2
exit 1
