#!/bin/sh
# A sandbox that reports a verdict the way isolate does: the run finishes and the meta file carries
# `status: OK`. Used to prove the supervisor reads the report rather than the exit code.
while [ "$#" -gt 0 ]; do
  if [ "$1" = "--meta" ]; then
    shift
    printf 'status:OK\ncode:0\ntime:0.031\nmax-rss:1024\n' >"$1"
    exit 0
  fi
  shift
done
exit 2
