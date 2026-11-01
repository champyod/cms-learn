#!/usr/bin/env bash
# A claim is exclusive and a dead worker's claim expires.
#
# The queue's correctness is not "a worker got a row"; it is that two workers never get the same row, that an
# empty queue hands out nothing, and that a row whose worker vanished comes back. The statement under test is
# extracted from judge/src/worker.cpp rather than written again here, because a second copy of the SQL would
# agree with itself and with nothing else.
#
# Needs PostgreSQL server binaries (initdb, pg_ctl) and pgschema; without them there is no cluster to claim
# against and the script exits 77.
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work="$(mktemp -d)"
port=$((56000 + (RANDOM % 500)))
database=cmslearn_claim_gate
worker=gate-worker
rows=4

cleanup() {
  pg_ctl -D "$work/data" -m immediate stop >/dev/null 2>&1
  rm -rf "$work"
}
trap cleanup EXIT

pass=0
fail=0
ok() { printf '  [ok]   %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  [!!]   %s — %s\n' "$1" "$2"; fail=$((fail + 1)); }
die() { printf 'claim gate FAILED: %s\n' "$*" >&2; exit 1; }

for tool in initdb pg_ctl psql pgschema sha256sum; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    printf 'cannot be exercised: %s is not installed\n' "$tool" >&2
    exit 77
  fi
done

# The statement and the lease come from the code that runs them: a gate against its own copy of the query
# would pass while the worker claimed something else.
claim_sql="$(awk '/R"claim\(/ { capture = 1; next } /\)claim";/ { capture = 0 } capture' \
  "$repo/judge/src/worker.cpp")"
[ -n "$claim_sql" ] || die "judge/src/worker.cpp no longer carries the claim statement between its markers"
lease="$(grep -o 'CLAIM_LEASE{[0-9]\+}' "$repo/judge/include/cms/judge/worker.hpp" | grep -o '[0-9]\+')"
[ -n "$lease" ] || die "judge/include/cms/judge/worker.hpp no longer declares CLAIM_LEASE"
backdate=$((lease * 3))

initdb -D "$work/data" -U postgres -A trust >"$work/initdb.log" 2>&1 || die "initdb failed; see $work/initdb.log"
pg_ctl -D "$work/data" -o "-c listen_addresses='' -c unix_socket_directories=$work -p $port" \
  -l "$work/server.log" -w start >/dev/null 2>&1 || die "the scratch cluster did not start; see $work/server.log"
psql -X -h "$work" -p "$port" -U postgres -q -v ON_ERROR_STOP=1 -f "$repo/deploy/roles.sql" >/dev/null \
  || die "deploy/roles.sql failed"
createdb -h "$work" -p "$port" -U postgres "$database" || die "could not create $database"
PGHOST="$work" PGPORT="$port" PGDATABASE="$database" PGUSER=postgres \
  bash "$repo/tools/apply-plan.sh" >"$work/apply.log" 2>&1 \
  || { tail -5 "$work/apply.log"; die "tools/apply-plan.sh failed"; }

psql_db() {
  psql -X -q -t -A -F '|' -h "$work" -p "$port" -U postgres -d "$database" -v ON_ERROR_STOP=1 "$@"
}

psql_db >"$work/seed.log" 2>&1 <<SQL || die "seeding the queue failed; see $work/seed.log"
INSERT INTO principals (id, kind, handle, status)
VALUES ('11111111-1111-4111-8111-111111111100', 'user', 'claim_gate', 'active');
INSERT INTO tasks (id, version, status, title_en, title_th, difficulty, time_limit_ms,
                   memory_limit_mb, checker, origin, verification, solution, content_hash)
VALUES ('claim-gate-task', 1, 'draft', 'Claim gate', 'ทดสอบเกต', 800, 1000, 256,
        'standard', 'original', 'pending', 'int main() { return 0; }', repeat('0', 64));
INSERT INTO submissions (account_id, task_id, task_version, source)
SELECT '11111111-1111-4111-8111-111111111100', 'claim-gate-task', 1, 'int main() { return 0; }'
FROM generate_series(1, $rows);
SQL

# One session per batch, because a prepared statement does not outlive the connection that prepared it.
claim_session() {
  local file="$1" count="$2" index
  {
    printf 'PREPARE claim(text, bigint) AS\n%s;\n' "$claim_sql"
    for ((index = 0; index < count; index++)); do
      printf "EXECUTE claim('%s', %s);\n" "$worker" "$lease"
    done
  } >"$file"
  psql_db -f "$file"
}

claims="$(claim_session "$work/claims.sql" "$((rows + 1))" 2>"$work/claims.err")"
[ -s "$work/claims.err" ] && die "the claim statement failed: $(tail -3 "$work/claims.err")"

taken="$(printf '%s\n' "$claims" | awk 'NF { n++ } END { print n + 0 }')"
distinct="$(printf '%s\n' "$claims" | awk -F'|' 'NF { print $1 }' | LC_ALL=C sort -u | awk 'NF { n++ } END { print n + 0 }')"
[ "$taken" = "$rows" ] \
  && ok "$rows claims took $rows row(s) and the next claim took none" \
  || bad "$rows claims take $rows rows and the next claim takes none" \
         "got $taken row(s): $(printf '%s' "$claims" | tr '\n' ' ')"
[ "$distinct" = "$rows" ] \
  && ok "no two claims took the same row" \
  || bad "no two claims took the same row" "$distinct distinct id(s) among $taken"

named="$(psql_db -c "SELECT count(*) FROM submissions WHERE claimed_by = '$worker'")"
[ "$named" = "$rows" ] \
  && ok "each claimed row records the worker that took it" \
  || bad "each claimed row records the worker that took it" "$named row(s) name $worker"

first_id="$(printf '%s\n' "$claims" | awk -F'|' 'NF { print $1; exit }')"
psql_db -c "UPDATE submissions SET claimed_at = now() - interval '$backdate minutes' WHERE id = '$first_id'" \
  >/dev/null || die "could not age the claim on $first_id"

reclaimed="$(claim_session "$work/reclaim.sql" 1 2>"$work/reclaim.err")"
[ -s "$work/reclaim.err" ] && die "the claim statement failed: $(tail -3 "$work/reclaim.err")"
reclaimed_id="$(printf '%s\n' "$reclaimed" | awk -F'|' 'NF { print $1; exit }')"
[ "$reclaimed_id" = "$first_id" ] \
  && ok "a claim older than the lease is claimable again" \
  || bad "a claim older than the lease is claimable again" \
         "expected $first_id, got '${reclaimed_id}'"

printf '\n%s assertion(s) behaved, %s did not\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
