#!/usr/bin/env bash
# One gate cycle per checkout.
#
# Two cycles in one checkout corrupt each other: a build tree rewritten under a running ctest, a
# falsification root staged twice, and two clang-tidy runs spending hours contending rather than
# working. This wrapper holds <checkout>/build/gate.lock for the whole command it is given; a second
# cycle waits and names the holder, or refuses at the timeout. The lock belongs to the checkout, so it
# holds across users; a holder that no longer exists is reclaimed instead of blocking forever.
#
# Usage: gate-lock.sh <label> <command> [args...]
#
# CMS_GATE_LOCK_TIMEOUT  seconds to wait for the holder before refusing (default 7200)
# CMS_GATE_LOCK_HELD     set for the child, so a composite gate that invokes another wrapped gate
#                        reuses the lock instead of deadlocking on itself

set -uo pipefail

if [ "$#" -lt 2 ]; then
  printf 'usage: gate-lock.sh <label> <command> [args...]\n' >&2
  exit 2
fi
label="$1"
shift

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
lock="$repo/build/gate.lock"

if [ "${CMS_GATE_LOCK_HELD:-}" = "$lock" ]; then
  exec "$@"
fi

mkdir -p "$repo/build" || { printf 'gate-lock: cannot create %s\n' "$repo/build" >&2; exit 1; }

token="$$-$(date +%s)-$RANDOM"
field() { sed -n "s/^$1=//p" "$lock/owner" 2>/dev/null | head -1; }
# shellcheck disable=SC2329
# Invoked from the traps below.
release() {
  if [ "$(field token)" = "$token" ]; then
    rm -rf "$lock"
  fi
}
trap release EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP

owner_is_alive() {
  local pid="$1" state status
  state="$(ps -o stat= -p "$pid" 2>/dev/null)"
  status=$?
  # A ps that cannot run at all must never be read as "the holder is gone": that would hand the lock to
  # every waiter at once. Only a ps that answered, and answered emptily, proves the process is gone.
  if [ "$status" -eq 127 ] || [ "$status" -gt 1 ]; then
    printf 'gate-lock: cannot ask ps about PID %s (exit %s); refusing to reclaim the lock\n' "$pid" "$status" >&2
    return 0
  fi
  [ -n "$state" ] || return 1
  case "$state" in
    Z*) return 1 ;;
  esac
  return 0
}

timeout="${CMS_GATE_LOCK_TIMEOUT:-7200}"
waited=0
announced=0
while :; do
  if mkdir "$lock" 2>/dev/null; then
    if ! printf 'pid=%s\nhost=%s\ncommand=%s\nstarted=%s\ncheckout=%s\ntoken=%s\n' \
      "$$" "$(hostname)" "$label" "$(LC_ALL=C date '+%Y-%m-%d %H:%M:%S')" "$repo" "$token" >"$lock/owner"; then
      rm -rf "$lock"
      printf 'gate-lock: cannot write %s/owner\n' "$lock" >&2
      exit 1
    fi
    break
  fi

  holder_pid="$(field pid)"
  holder_host="$(field host)"
  holder_cmd="$(field command)"
  holder_started="$(field started)"

  if [ -z "$holder_pid" ]; then
    # An owner file that is not written yet is a lock being created; one that stays empty belongs to a
    # run that died between mkdir and the write.
    sleep 1
    if [ -z "$(field pid)" ]; then
      printf 'gate-lock: reclaiming %s — it has no owner, so the run that made it died before writing one\n' "$lock"
      if mv "$lock" "$lock.stale.$$" 2>/dev/null; then rm -rf "$lock.stale.$$"; fi
    fi
    continue
  fi

  if [ "$holder_host" = "$(hostname)" ] && ! owner_is_alive "$holder_pid"; then
    printf 'gate-lock: reclaiming the lock left by %s (PID %s, started %s) — that process is no longer running\n' \
      "$holder_cmd" "$holder_pid" "$holder_started"
    if mv "$lock" "$lock.stale.$$" 2>/dev/null; then rm -rf "$lock.stale.$$"; fi
    continue
  fi

  if [ "$announced" -eq 0 ]; then
    printf 'gate-lock: waiting for %s (PID %s, started %s) — it holds the gate lock for this checkout\n' \
      "$holder_cmd" "$holder_pid" "$holder_started"
    announced=1
  elif [ "$waited" -gt 0 ] && [ $((waited % 60)) -eq 0 ]; then
    printf 'gate-lock: still waiting for %s (PID %s), %ss elapsed\n' "$holder_cmd" "$holder_pid" "$waited"
  fi

  if [ "$waited" -ge "$timeout" ]; then
    printf 'gate-lock: refused %s — %s (PID %s) has held the gate lock for this checkout for %ss (CMS_GATE_LOCK_TIMEOUT=%s)\n' \
      "$label" "$holder_cmd" "$holder_pid" "$waited" "$timeout" >&2
    exit 1
  fi
  sleep 2
  waited=$((waited + 2))
done

export CMS_GATE_LOCK_HELD="$lock"
"$@" &
child=$!
# shellcheck disable=SC2329
# Invoked from the traps below.
forward() { kill "$child" 2>/dev/null; }
trap 'forward; exit 130' INT
trap 'forward; exit 143' TERM
trap 'forward; exit 129' HUP
wait "$child"
status=$?
exit "$status"
