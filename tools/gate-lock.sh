#!/usr/bin/env bash
# One gate cycle per checkout.
#
# Two cycles in one checkout corrupt each other: a build tree rewritten under a running ctest, a
# falsification root staged twice, and two clang-tidy runs spending hours contending rather than
# working. This wrapper holds <checkout>/build/gate.lock for the whole command it is given; a second
# cycle waits and names the holder, or refuses at the timeout. The lock belongs to the checkout, so it
# holds across users; a holder that no longer exists is reclaimed instead of blocking forever.
#
# A pid is a slot, not a process. On 2026-11-20 an interrupted check:live left a lock recording pid 5,
# started 01:51; the number still existed inside the container's PID namespace and belonged to an
# unrelated process, so the old liveness test ("is there a process with this number?") answered yes and
# the next gate waited out its 7200 s timeout until the file was cleared by hand. The owner file
# therefore records the holder's start time from /proc/<pid>/stat and the boot it was taken in, and a
# number whose start time or boot differs is somebody else's process and is reclaimed.
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

# The boot the lock was taken in. A start time is measured from boot, so two of them are only comparable within one.
boot_id() { cat /proc/sys/kernel/random/boot_id 2>/dev/null; }

# @return The start time the kernel records for @p pid, in clock ticks since boot, or nothing when the process does not
# exist or the kernel does not expose it. The command name in /proc/<pid>/stat is parenthesised and may contain spaces,
# so the fields are counted from the last ')'.
process_start_time() {
  local pid="$1" stat rest
  [ -r "/proc/$pid/stat" ] || return 1
  stat="$(cat "/proc/$pid/stat" 2>/dev/null)" || return 1
  rest="${stat##*) }"
  # shellcheck disable=SC2086  # every /proc/<pid>/stat field is one token; word splitting is the parse.
  set -- $rest
  [ "$#" -ge 20 ] || return 1
  printf '%s' "${20}"
}

# @return The command line @p pid runs, so a reclaim can name what the number belongs to now.
process_command() {
  local pid="$1" command
  [ -r "/proc/$pid/cmdline" ] || return 1
  command="$(tr '\0' ' ' <"/proc/$pid/cmdline" 2>/dev/null)" || return 1
  printf '%s' "${command% }"
}

# Prints why the recorded holder is gone and returns 0; prints nothing and returns 1 when it is still the process that
# took the lock. @p recorded_start and @p recorded_boot are empty for a lock written before this check existed, which
# then falls back to the number alone.
holder_gone_reason() {
  local pid="$1" recorded_start="$2" recorded_boot="$3" current_boot state status start command
  current_boot="$(boot_id)"
  if [ -n "$recorded_boot" ] && [ -n "$current_boot" ] && [ "$current_boot" != "$recorded_boot" ]; then
    printf 'the machine has booted since the lock was taken, so no process from that boot survives'
    return 0
  fi
  state="$(ps -o stat= -p "$pid" 2>/dev/null)"
  status=$?
  # A ps that cannot run at all must never be read as "the holder is gone": that would hand the lock to every waiter
  # at once. Only a ps that answered, and answered emptily, proves the process is gone.
  if [ "$status" -eq 127 ] || [ "$status" -gt 1 ]; then
    printf 'gate-lock: cannot ask ps about PID %s (exit %s); refusing to reclaim the lock\n' "$pid" "$status" >&2
    return 1
  fi
  if [ -z "$state" ]; then
    printf 'no process with that number is running'
    return 0
  fi
  case "$state" in
    Z*) printf 'that process is a zombie, so the gate it ran is already over'; return 0 ;;
  esac
  if [ -n "$recorded_start" ]; then
    start="$(process_start_time "$pid")"
    if [ -n "$start" ] && [ "$start" != "$recorded_start" ]; then
      command="$(process_command "$pid")"
      printf 'PID %s now belongs to "%s", started at %s, and the holder started at %s, so that is another process' \
        "$pid" "${command:-an unreadable command}" "$start" "$recorded_start"
      return 0
    fi
  fi
  return 1
}

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

timeout="${CMS_GATE_LOCK_TIMEOUT:-7200}"
waited=0
announced=0
while :; do
  if mkdir "$lock" 2>/dev/null; then
    if ! printf 'pid=%s\nhost=%s\ncommand=%s\nstarted=%s\ncheckout=%s\ntoken=%s\nboot=%s\nstarttime=%s\n' \
      "$$" "$(hostname)" "$label" "$(LC_ALL=C date '+%Y-%m-%d %H:%M:%S')" "$repo" "$token" "$(boot_id)" \
      "$(process_start_time "$$")" >"$lock/owner"; then
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
  holder_boot="$(field boot)"
  holder_start="$(field starttime)"

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

  if [ "$holder_host" = "$(hostname)" ] &&
    reason="$(holder_gone_reason "$holder_pid" "$holder_start" "$holder_boot")"; then
    printf 'gate-lock: reclaiming the lock left by %s (PID %s, started %s): %s\n' \
      "$holder_cmd" "$holder_pid" "$holder_started" "$reason"
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
# Invoked from the traps above.
forward() { kill "$child" 2>/dev/null; }
trap 'forward; exit 130' INT
trap 'forward; exit 143' TERM
trap 'forward; exit 129' HUP
wait "$child"
status=$?
exit "$status"
