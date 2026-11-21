#!/usr/bin/env bash
# A draft becomes published only through a certificate row the certifier wrote.
#
# The order of checks is the point: a task whose certificate row is missing or does not say verified is refused
# before its cases are read, and a reference that does not reproduce its expected output stops the publish
# afterwards. The task file carries no verification claim any more (ADR 0046 §2): the certificate row is the only
# source, so the last fixture is deliberately illegal — its manifest still names a claim — and the point is that it
# publishes anyway on a verified row, because the promoter never reads the file. The artifact is named by the digest
# of the published bytes, so publishing twice yields one path, not two.
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
generator="${1:-$repo/build/safety/judge/cms_generate_task}"
promoter="${2:-$repo/build/safety/judge/cms_promote_task}"
reference="${3:-$repo/build/safety/references/ref_range_sum_query_main}"

for tool in "$generator" "$promoter" "$reference"; do
  [ -x "$tool" ] || { echo "publish gate FAILED: $tool is not executable" >&2; exit 1; }
done

# shellcheck source=tools/postgres-cluster.sh
. "$repo/tools/postgres-cluster.sh"

pg_cluster_binaries
for tool in initdb pg_ctl psql; do
  command -v "$tool" >/dev/null 2>&1 || { echo "  skipped: $tool is not installed"; exit 77; }
done

work="$(mktemp -d)"
port=$((56100 + (RANDOM % 200)))
database=cmslearn_publish_gate

cleanup() {
  pg_cluster_stop "$work" >/dev/null 2>&1 || true
  rm -rf "$work"
}
trap cleanup EXIT

pass=0
fail=0
ok() { printf '  [ok]   %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  [!!]   %s — %s\n' "$1" "$2"; fail=$((fail + 1)); }

pg_cluster_prepare "$work" || { echo "  [!!] the cluster cannot be handed to the postgres user"; exit 1; }
pg_cluster_init "$work" >"$work/initdb.log" 2>&1 || { echo "  [!!] initdb failed"; exit 1; }
pg_cluster_start "$work" "$port" "$work/pg.log" >/dev/null 2>&1 || { echo "  [!!] pg_ctl failed"; exit 1; }
psql -X -h "$work" -p "$port" -U postgres -q -v ON_ERROR_STOP=1 -f "$repo/deploy/roles.sql" >/dev/null 2>&1 \
  || { echo "  [!!] deploy/roles.sql failed"; exit 1; }
psql -X -h "$work" -p "$port" -U postgres -q -c "CREATE DATABASE $database" >/dev/null 2>&1 \
  || { echo "  [!!] the scratch database was not created"; exit 1; }
PGHOST="$work" PGPORT="$port" PGDATABASE="$database" PGUSER=postgres \
  bash "$repo/tools/apply-plan.sh" >"$work/apply.log" 2>&1 \
  || { echo "  [!!] apply-plan failed"; tail -5 "$work/apply.log"; exit 1; }

# The promoter reads the certificate row, so every invocation below carries the deployer's database in front of it.
export CMSLEARN_DATABASE_URL="host=$work port=$port dbname=$database user=postgres"

psql_db() { psql -X -h "$work" -p "$port" -U postgres -d "$database" -tAq "$@"; }

# A certificate names a task version, so the row it certifies has to exist first. The generator writes files and
# never a database row, which is the same split the pipeline has.
given_task() {
  psql_db -c "INSERT INTO tasks (id, version, status, title_en, title_th, difficulty, time_limit_ms, memory_used_limit_mb, memory_reserved_limit_mb,
                                 checker, origin, verification, solution, content_hash)
              VALUES ('$1', 1, 'draft', 'Probe', 'ทดสอบ', 900, 1000, 256, 512, 'token', 'original', 'pending', '', '')
              ON CONFLICT (id, version) DO NOTHING" >/dev/null 2>&1 \
    || { echo "  [!!] the gate could not write the task row for $1"; exit 1; }
}

given_certificate() {
  psql_db -c "INSERT INTO generation_jobs (task_id, task_version) VALUES ('$1', 1)
              ON CONFLICT (task_id, task_version) DO NOTHING" >/dev/null 2>&1 \
    || { echo "  [!!] the gate could not write the job row for $1"; exit 1; }
  psql_db -c "INSERT INTO generation_certificates (job_id, task_id, task_version, outcome, checks, commit_id, evidence)
              SELECT id, '$1', 1, '$2', '[\"the cases read back\"]'::jsonb, '$3', '{}'::jsonb
              FROM generation_jobs WHERE task_id = '$1' ORDER BY id DESC LIMIT 1" >/dev/null 2>&1 \
    || { echo "  [!!] the gate could not write the $2 certificate for $1"; exit 1; }
}

printf '3 2\n1 2 3\n1 2\n2 3\n' >"$work/example.in"
printf '3\n5\n' >"$work/example.out"
printf 'ให้ลำดับตัวเลขและคำถาม\n' >"$work/statement.th"
printf 'Given a sequence and queries\n' >"$work/statement.en"
cp "$repo/content/tasks/range-sum-query/solutions/main.cpp" "$work/main.cpp"

"$generator" --out "$work/draft" --id probe-task --title-en "Probe" --title-th "ทดสอบ" \
  --technique prefix-sums --topics "prefix-sums" --difficulty 900 --time-ms 1000 --memory-used-mb 256 --memory-reserved-mb 512 --checker token \
  --statement-en "$work/statement.en" --statement-th "$work/statement.th" \
  --example-in "$work/example.in" --example-out "$work/example.out" --solution "$work/main.cpp" \
  >/dev/null 2>&1 || { echo "publish gate FAILED: the generator refused the specification" >&2; exit 1; }
given_task probe-task

# The plan's E7.2 falsification: no certificate row exists, so publication must be refused.
if "$promoter" "$work/draft" "$work/artifacts" "$reference" >"$work/pending.out" 2>&1; then
  bad "a task with no certificate row is refused" "the promoter published it anyway"
else
  ok "a task with no certificate row is refused"
fi
grep -q "no certificate row exists for probe-task version 1" "$work/pending.out" \
  && ok "the refusal names the certificate it looked for" \
  || bad "the refusal names the certificate it looked for" "$(cat "$work/pending.out")"
if [ -d "$work/artifacts" ] && [ -n "$(ls -A "$work/artifacts" 2>/dev/null)" ]; then
  bad "a refused publish writes no artifact" "$(ls "$work/artifacts")"
else
  ok "a refused publish writes no artifact"
fi

# The file carries no claim to compare against, so this case is simply a failed certificate over a draft file.
given_certificate probe-task failed deadbeef
if "$promoter" "$work/draft" "$work/artifacts" "$reference" >"$work/disagree.out" 2>&1; then
  bad "a file that says verified over a failed certificate is refused" "the promoter published it"
else
  ok "a file that says verified over a failed certificate is refused"
fi
grep -q 'the certificate row records "failed"' "$work/disagree.out" \
  && ok "the refusal names the certificate row's outcome" \
  || bad "the refusal names the certificate row's outcome" "$(cat "$work/disagree.out")"
if [ -d "$work/artifacts" ] && [ -n "$(ls -A "$work/artifacts" 2>/dev/null)" ]; then
  bad "a refused publish writes no artifact over a failed certificate" "$(ls "$work/artifacts")"
else
  ok "a refused publish writes no artifact over a failed certificate"
fi

# With a verified certificate recorded, the same task publishes and the artifact is named by its own digest.
given_certificate probe-task verified a1b2c3d4
if "$promoter" "$work/draft" "$work/artifacts" "$reference" >/dev/null 2>&1; then
  ok "a verified certificate publishes the task"
else
  bad "a verified certificate publishes the task" "the promoter refused a verified certificate"
fi

digest="$(ls "$work/artifacts" 2>/dev/null | head -1)"
case "$digest" in
  [0-9a-f][0-9a-f]*) ok "the artifact is content-addressed" ;;
  *) bad "the artifact is content-addressed" "no digest-named file was written" ;;
esac
grep -q '"status": "published"' "$work/draft/task.json" \
  && ok "the manifest records the task as published" \
  || bad "the manifest records the task as published" "the status did not change"

# Publishing again must not rewrite an already published task.
if "$promoter" "$work/draft" "$work/artifacts" "$reference" >/dev/null 2>&1; then
  bad "an already published task is refused" "the promoter republished it"
else
  ok "an already published task is refused"
fi

# ADR 0046 §2 retired the file's verification claim, so this fixture is deliberately illegal: its manifest still
# names one. It publishes anyway on a verified certificate row, because the promoter reads the row and never the file.
"$generator" --out "$work/claim" --id probe-claim --title-en "Probe" --title-th "ทดสอบ" \
  --technique prefix-sums --topics "prefix-sums" --difficulty 900 --time-ms 1000 --memory-used-mb 256 --memory-reserved-mb 512 --checker token \
  --statement-en "$work/statement.en" --statement-th "$work/statement.th" \
  --example-in "$work/example.in" --example-out "$work/example.out" --solution "$work/main.cpp" \
  >/dev/null 2>&1 \
  || { echo "publish gate FAILED: the generator refused the claiming draft" >&2; exit 1; }
sed -i 's|"verification": { "reference_solution": "solutions/main.cpp" }|"verification": { "status": "verified", "reference_solution": "solutions/main.cpp", "methods": [] }|' "$work/claim/task.json"
given_task probe-claim
given_certificate probe-claim verified c0ffee00
if "$promoter" "$work/claim" "$work/artifacts-claim" "$reference" >"$work/claim.out" 2>&1; then
  ok "a draft file that still names a claim publishes on a verified row"
else
  bad "a draft file that still names a claim publishes on a verified row" "$(cat "$work/claim.out")"
fi
grep -q '"status": "published"' "$work/claim/task.json" \
  && ok "the claiming file is published, and the file's claim decided nothing" \
  || bad "the claiming file is published, and the file's claim decided nothing" "the status did not change"

# A reference that does not reproduce its expected output stops the publish, even with a verified certificate.
"$generator" --out "$work/wrong" --id probe-wrong --title-en "Probe" --title-th "ทดสอบ" \
  --technique prefix-sums --topics "prefix-sums" --difficulty 900 --time-ms 1000 --memory-used-mb 256 --memory-reserved-mb 512 --checker token \
  --statement-en "$work/statement.en" --statement-th "$work/statement.th" \
  --example-in "$work/example.in" --example-out "$work/example.out" --solution "$work/main.cpp" \
  >/dev/null 2>&1 \
  || { echo "publish gate FAILED: the generator refused the wrong-reference draft" >&2; exit 1; }
[ -f "$work/wrong/examples/1.out" ] \
  || { echo "publish gate FAILED: the wrong-reference draft was not written" >&2; exit 1; }
printf '999\n' >"$work/wrong/examples/1.out"
given_task probe-wrong
given_certificate probe-wrong verified beefcafe
if "$promoter" "$work/wrong" "$work/artifacts" "$reference" >"$work/wrong.out" 2>&1; then
  bad "a reference that does not reproduce its output is refused" "the promoter published it"
else
  ok "a reference that does not reproduce its output is refused"
fi
grep -q "does not produce the expected output" "$work/wrong.out" \
  && ok "the refusal names the case the reference missed" \
  || bad "the refusal names the case the reference missed" "$(cat "$work/wrong.out")"

printf '\n%s assertion(s) behaved, %s did not\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
