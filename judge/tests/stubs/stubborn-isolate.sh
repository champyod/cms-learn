#!/bin/sh
# A sandbox that refuses to end: it ignores SIGTERM and outlives any sensible wall limit. The supervisor
# must escalate to SIGKILL, which is the behaviour the fork-bomb case exists to protect.
trap '' TERM
sleep 60
