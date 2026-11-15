#!/usr/bin/env bash
# The queue-claim proof, as the pipeline gate's falsification cases run it.
#
# The claim statement is read out of the source the product compiles, not copied into this script: a copy would
# keep passing after the product's query changed. It is then run against a real schema, twice: the first call takes
# the queued job, the second gets nothing while the lease lives, and after the lease expires the second takes it and
# the attempt count says two. A claim with its state and lease predicate stripped hands the same job out twice,
# which is the defect the "claim-drops-the-state-and-lease-predicate" case plants and this script exists to catch.
set -euo pipefail

root="$(pwd)"

fail() { printf 'claim proof FAILED: %s\n' "$*" >&2; exit 1; }
note() { printf '  %s\n' "$*"; }

# shellcheck source=../postgres-cluster.sh
. "$root/tools/postgres-cluster.sh"

work="$(mktemp -d)"
cleanup() {
  pg_cluster_stop "$work" >/dev/null 2>&1 || true
  rm -rf "$work"
}
trap cleanup EXIT

# ── the statement the product sends ──────────────────────────────────────────────────────────────
claim="$work/claim.sql"
awk '/R"claim\(/{inside=1; next} /\)claim";/{inside=0} inside' "$root/generator/src/claim.cpp" >"$claim"
[ -s "$claim" ] || fail "generator/src/claim.cpp holds no claim statement to prove"
grep -q 'UPDATE generation_jobs' "$claim" ||
  fail "the statement in generator/src/claim.cpp does not update generation_jobs"

# ── a real schema to run it against ─────────────────────────────────────────────────────────────
port=$((58600 + (RANDOM % 200)))
pg_cluster_binaries
for tool in initdb pg_ctl psql createdb; do
  command -v "$tool" >/dev/null 2>&1 || fail "$tool is not installed, so the claim cannot be proven"
done
pg_cluster_prepare "$work" >/dev/null || fail "the cluster cannot be handed to the postgres user"
pg_cluster_init "$work" >/dev/null 2>&1 || fail "initdb failed"
pg_cluster_start "$work" "$port" "$work/pg.log" >/dev/null 2>&1 || fail "the scratch cluster did not start"
psql -X -h "$work" -p "$port" -U postgres -q -f "$root/deploy/roles.sql" >/dev/null 2>&1
createdb -h "$work" -p "$port" -U postgres claim_proof >/dev/null 2>&1
PGHOST="$work" PGPORT="$port" PGDATABASE=claim_proof PGUSER=postgres \
  bash "$root/tools/apply-plan.sh" >/dev/null 2>&1 || fail "the schema did not apply"

sql() { psql -X -h "$work" -p "$port" -U postgres -d claim_proof -tAq -c "$1"; }

sql "INSERT INTO tasks (id, version, status, title_en, title_th, difficulty, time_limit_ms, memory_limit_mb, checker, origin, verification, solution, content_hash) VALUES ('claim-probe', 1, 'draft', 'Claim probe', 'ทดสอบ', 900, 1000, 256, 'token', 'original', 'pending', '', '')" >/dev/null
sql "INSERT INTO generation_jobs (task_id, task_version) VALUES ('claim-probe', 1)" >/dev/null

# The statement with its two parameters bound the way the driver binds them.
bound() { sed -e "s/\\\$1/'$1'/g" -e "s/\\\$2::int/600/g" "$claim"; }

first="$(sql "$(bound worker-one)")"
[ -n "$first" ] || fail "the first claim took nothing from a queued job"
second="$(sql "$(bound worker-two)")"
[ -z "$second" ] || fail "the claim handed the same job to a second worker while its lease was live"

sql "UPDATE generation_jobs SET lease_expires_at = now() - interval '1 minute'" >/dev/null
third="$(sql "$(bound worker-two)")"
[ -n "$third" ] || fail "a claim whose lease expired was not taken over"
attempts="$(sql "SELECT attempts FROM generation_jobs")"
[ "$attempts" = "2" ] || fail "the takeover did not count a second attempt (attempts=$attempts)"

# The behaviour above is the proof; these say which half of the statement a rewrite dropped, so a failure names the
# rule rather than only the symptom.
grep -q "state = 'queued'" "$claim" || fail "the claim has no queued predicate"
grep -q 'lease_expires_at < now()' "$claim" || fail "the claim has no expired-lease predicate"
grep -q 'FOR UPDATE SKIP LOCKED' "$claim" ||
  fail "the claim does not skip a row another worker is claiming (FOR UPDATE SKIP LOCKED is missing)"

note "the claim takes a queued job once, refuses while the lease lives, and takes it over when it expires"
