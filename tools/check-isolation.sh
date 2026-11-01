#!/usr/bin/env bash
# A fork bomb must be contained by the sandbox and the box torn down afterwards.
#
# This runs in the container that ships the judge, not on the host: isolate creates namespaces and cgroups,
# which is a privileged operation, and measuring it anywhere else would prove something about that machine
# rather than about the judge. The C++ supervisor tests cover the argument list, the deadline and the
# escalation with stubs; this covers the containment claim itself.
#
# Skips (exit 77) when Docker is not available, the same way the database gates skip without PostgreSQL.
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
image="cms-learn-isolate:check"

command -v docker >/dev/null 2>&1 || { echo "  skipped: docker is not installed"; exit 77; }
docker info >/dev/null 2>&1 || { echo "  skipped: no docker daemon is reachable"; exit 77; }

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

if ! docker build --tag "$image" "$repo/judge/isolation" >"$work/build.log" 2>&1; then
  echo "isolation gate FAILED: the sandbox image did not build" >&2
  tail -5 "$work/build.log" >&2
  exit 1
fi

# The bomb: a function that pipes itself into itself in the background, forever. One process is allowed, so
# the sandbox must refuse the tree and report that the program did not exit cleanly.
cat >"$work/bomb.sh" <<'BOMB'
#!/bin/sh
bomb() { bomb | bomb & }
bomb
BOMB
chmod +x "$work/bomb.sh"

read -r -d '' probe <<'PROBE'
set -u
box="$(isolate --init --box-id 0)"
cp /bomb.sh "$box/bomb.sh"
chmod +x "$box/bomb.sh"
status=0
isolate --box-id 0 --processes 1 --time 1 --wall-time 3 --mem 131072 \
  --stdout out.txt --stderr err.txt --meta meta.txt --run -- ./bomb.sh || status=$?
verdict="$(sed -n 's/^status:\(.*\)$/\1/p' meta.txt)"
echo "exit=$status status=$verdict"
isolate --cleanup --box-id 0 >/dev/null 2>&1
[ -d "$box" ] && { echo "leftover-box=1"; exit 9; }
case "$status:$verdict" in
  *:SG|*:TO|*:RE|*:XX) exit 0 ;;
  *) exit 8 ;;
esac
PROBE

output="$(docker run --rm --privileged --entrypoint bash -v "$work/bomb.sh:/bomb.sh:ro" "$image" -c "$probe" 2>&1)"
code=$?

if [ "$code" -eq 0 ]; then
  printf '  [ok]   the fork bomb was contained (%s)\n' "$output"
  printf 'isolation gate: OK\n'
  exit 0
fi

printf 'isolation gate FAILED: the sandbox did not contain the bomb (exit %s)\n' "$code" >&2
printf '%s\n' "$output" >&2
exit 1
