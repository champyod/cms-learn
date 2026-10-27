# Health signal for the backup sidecar.
#
# "The container is running" is not the question — a sidecar that has silently stopped taking dumps
# looks perfectly healthy from the outside. Two facts are checked:
#   1. the scheduler process is alive
#   2. the newest archive is younger than 36h (one missed night is tolerated, two is not)
# A fresh install has no archive yet, and that is reported as healthy-but-empty rather than a failure.
set -eu

pgrep -f '/usr/local/bin/run.sh' >/dev/null 2>&1 || {
  echo "run.sh is not running"
  exit 1
}

newest="$(ls -1t /backups/cms-learn-*.dump.age 2>/dev/null | head -1 || true)"
if [ -z "$newest" ]; then
  echo "no archive yet (fresh install)"
  exit 0
fi

age_hours=$(( ( $(date +%s) - $(date -r "$newest" +%s) ) / 3600 ))
if [ "$age_hours" -ge 36 ]; then
  echo "newest archive is ${age_hours}h old: ${newest}"
  exit 1
fi

echo "newest archive ${age_hours}h old: $(basename "$newest")"
exit 0
