#!/bin/sh
# A local stand-in for isolate, for the judge gate: the same flags, no namespaces or cgroups.
#
# --init prints a fresh box directory, --cleanup removes it, and a run executes the program inside the box
# with the streams isolate would have redirected. It reports status through the meta file, like isolate.
set -u

base="${TMPDIR:-/tmp}/cms-local-isolate"
mode=""
box=""
meta=""
dir=""
stdin_file=""
stdout_file=""
stderr_file=""

while [ "$#" -gt 0 ]; do
  case "$1" in
    --init|--cleanup) mode="$1"; shift ;;
    --box-id) box="$2"; shift 2 ;;
    --meta) meta="$2"; shift 2 ;;
    --stdin) stdin_file="$2"; shift 2 ;;
    --stdout) stdout_file="$2"; shift 2 ;;
    --stderr) stderr_file="$2"; shift 2 ;;
    --time|--wall-time|--mem|--processes) shift 2 ;;
    --dir) dir="${2#box=}"; shift 2 ;;
    --run) shift ;;
    --) shift; break ;;
    *) break ;;
  esac
done

[ -n "$box" ] || exit 2
box_dir="$base/box-$box"

case "$mode" in
  --init)
    mkdir -p "$box_dir"
    printf '%s\n' "$box_dir"
    exit 0
    ;;
  --cleanup)
    rm -rf "$box_dir"
    exit 0
    ;;
esac

[ -n "$dir" ] || exit 2
cd "$dir" || exit 2
program="$1"
shift

"$program" "$@" <"${stdin_file:-/dev/null}" >"${stdout_file:-/dev/null}" 2>"${stderr_file:-/dev/null}"
status=$?

if [ "$status" -eq 0 ]; then
  verdict=OK
else
  verdict=RE
fi
if [ -n "$meta" ]; then
  printf 'status:%s\ncode:%s\ntime:0.000\nmax-rss:0\n' "$verdict" "$status" >"$meta"
fi
exit 0
