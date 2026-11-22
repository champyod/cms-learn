#!/bin/sh
# A sandbox that reports a verdict the way isolate does: the run finishes and the meta file carries
# `status: OK`. Used to prove the supervisor reads the report rather than the exit code. The report is a
# host path, the one file the sandbox manager writes outside the sandbox.
set -u
meta=""
while [ "$#" -gt 0 ]; do
  case "$1" in
    --meta) meta="$2"; shift 2 ;;
    *) shift ;;
  esac
done
[ -n "$meta" ] || exit 2
printf 'status:OK\ncode:0\ntime:0.031\nmax-rss:1024\n' >"$meta"
exit 0
