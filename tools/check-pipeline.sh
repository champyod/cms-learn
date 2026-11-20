#!/usr/bin/env bash
# The generation pipeline's gate.
#
# ADR 0028 says no vendor, endpoint, model id, region or key of ours enters the product; ADR 0015 says the worker
# records what each step produced. This gate asserts both from the outside: it greps the tree for a vendor, it
# proves the worker's role cannot publish or verify, and it proves the worker refuses to start without the
# deployer's configuration.
#
# Needs PostgreSQL server binaries for the role half; without them that half is reported as not run, not passed.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

fail() { printf 'pipeline gate FAILED: %s\n' "$*" >&2; exit 1; }
note() { printf '  %s\n' "$*"; }

# shellcheck source=tools/postgres-cluster.sh
. "$repo/tools/postgres-cluster.sh"

work="$(mktemp -d)"

cleanup() {
  pg_cluster_stop "$work" >/dev/null 2>&1 || true
  rm -rf "$work"
}
trap cleanup EXIT

worker="${CMS_GENERATE_BINARY:-$repo/build/safety/generator/cms_generate}"
[ -x "$worker" ] || fail "no generator worker at $worker; build it with: cmake --build --preset safety"

# ── 1. no vendor of ours ────────────────────────────────────────────────────────────────────────
# A default endpoint, model name or region compiled in is the mistake ADR 0028 exists to prevent. The patterns are
# vendor names and key shapes, never a value of a deployer's. The falsifiers are the one place allowed to name a
# vendor, because naming one is how they prove this gate bites.
vendors='api\.openai\.com|openai\.azure\.com|anthropic\.com|generativelanguage\.googleapis\.com|bedrock-runtime|dashscope|api\.mistral\.ai|api\.groq\.com|openrouter\.ai|api\.together\.(ai|xyz)|fireworks\.ai|sk-[A-Za-z0-9]{16,}|AKIA[0-9A-Z]{16}'
hits="$(grep -rlnE "$vendors" \
  "$repo/api" "$repo/bank" "$repo/core" "$repo/generator" "$repo/judge" "$repo/deploy" "$repo/tools" \
  --include='*.cpp' --include='*.hpp' --include='*.ts' --include='*.sh' --include='*.yml' --include='*.yml' \
  --include='*.json' --include='*.example' 2>/dev/null \
  | grep -v "^$repo/tools/check-pipeline.sh$" \
  | grep -v "^$repo/tools/falsify/cases/" || true)"
[ -z "$hits" ] || fail "a vendor of ours appears in the tree: $(printf '%s' "$hits" | tr '\n' ' ')"
note "no model vendor, endpoint or key of ours anywhere in the tree"

# ── 2. the boundary: the service cannot hold a model key ───────────────────────────────────────
if grep -rln "cms/generate/" "$repo/api/src" "$repo/api/include" 2>/dev/null | grep -q .; then
  fail "the service includes the generation client; the api must never hold a model key (ADR 0028)"
fi
note "the api does not include the generation client"

# ── 3. the worker refuses to start without the deployer's configuration ────────────────────────
set +e
refusal="$(env -i "$worker" --check 2>&1)"
status=$?
set -e
[ "$status" -eq 2 ] || fail "the worker started without configuration (exit $status)"
# The model is the operator's record, so its four keys are gone from the configuration surface: the refusal names the
# nine that remain, and an operator's first instruction is the page rather than an export.
for key in CMSLEARN_DATABASE_URL CMSLEARN_CERTIFIER_DATABASE_URL CMSLEARN_S3_ENDPOINT CMSLEARN_CONTENT_ROOT CMSLEARN_COMMIT; do
  printf '%s' "$refusal" | grep -q "$key" || fail "the refusal does not name $key: $refusal"
done
printf '%s' "$refusal" | grep -q "secret" && fail "the refusal printed a value"
note "the worker refuses to start and names every missing key, printing no value"

# ── 4. the worker's role cannot publish or verify ───────────────────────────────────────────────
port=$((64800 + (RANDOM % 80)))
pg_cluster_binaries
for tool in initdb pg_ctl psql createdb; do
  command -v "$tool" >/dev/null 2>&1 || {
    note "the role half did not run: PostgreSQL binaries are not installed ($tool is missing)"
    fail "the role half of the pipeline gate did not run; a gate that passes without it proves nothing"
  }
done
pg_cluster_prepare "$work" || fail "the cluster cannot be handed to the postgres user"
pg_cluster_init "$work" >/dev/null 2>&1 || fail "initdb failed"
pg_cluster_start "$work" "$port" "$work/pg.log" >/dev/null 2>&1 || fail "the scratch cluster did not start"
psql -X -h "$work" -p "$port" -U postgres -q -f "$repo/deploy/roles.sql" >/dev/null 2>&1
createdb -h "$work" -p "$port" -U postgres pipeline_gate >/dev/null 2>&1
PGHOST="$work" PGPORT="$port" PGDATABASE=pipeline_gate PGUSER=postgres \
  bash "$repo/tools/apply-plan.sh" >/dev/null 2>&1 || fail "the schema did not apply"

as_generator() {
  psql -X -h "$work" -p "$port" -U postgres -d pipeline_gate -tAq \
    -c "SET ROLE cmslearn_generator" -c "$1" 2>&1
}

# One valid value per column, so the refusal can only be the privilege and never an enum the database rejects.
for attempt in "status published" "verification verified"; do
  column="${attempt%% *}"
  value="${attempt##* }"
  # The refusal is the expected outcome, so the command's non-zero exit must not end the gate here.
  refusal="$(as_generator "UPDATE tasks SET $column = '$value'" || true)"
  printf '%s' "$refusal" | grep -q "permission denied for table tasks" \
    || fail "the generator role wrote tasks.$column: $refusal"
done
note "the generator role cannot write tasks.status or tasks.verification: permission denied for table tasks"

# The same role must be able to do its own job, or the refusal above would just mean it can do nothing.
# A job row needs a task to hang from, so one is written as the schema owner first.
psql -X -h "$work" -p "$port" -U postgres -d pipeline_gate -q -c \
  "INSERT INTO tasks (id, version, status, title_en, title_th, difficulty, time_limit_ms, memory_limit_mb, checker, origin, verification, solution, content_hash) VALUES ('probe-task', 1, 'draft', 'Probe', 'ทดสอบ', 900, 1000, 256, 'token', 'original', 'pending', '', '')" >/dev/null 2>&1 \
  || fail "the gate could not write the probe task"
set +e
as_generator "INSERT INTO generation_jobs (task_id, task_version) VALUES ('probe-task', 1)" >/dev/null 2>&1
wrote=$?
set -e
[ "$wrote" -eq 0 ] || fail "the generator role cannot write its own record"
rows="$(psql -X -h "$work" -p "$port" -U postgres -d pipeline_gate -tAq -c "SELECT count(*) FROM generation_jobs")"
[ "$rows" = "1" ] || fail "the generator role reported success and wrote no job row"
note "the generator role writes its own record"

# ── 5. the certifier is a role of its own, and a verification needs its certificate ───────────────
# The generator holds no UPDATE on tasks, and the certifier holds the one column a verification is: the two grants
# are read out of the reviewed schema, not assumed.
grep -q "GRANT UPDATE (verification) ON tasks TO cmslearn_certifier" "$repo/deploy/schema/generation.sql" \
  || fail "the schema does not grant cmslearn_certifier UPDATE (verification) on tasks"
if grep -q "GRANT .*UPDATE.* ON tasks TO cmslearn_generator" "$repo/deploy/schema/generation.sql"; then
  fail "the generator role is granted an UPDATE on tasks; the barrier is gone"
fi
note "the certifier holds UPDATE (verification) on tasks; the generator holds no UPDATE on it"

roles_present="$(psql -X -h "$work" -p "$port" -U postgres -d pipeline_gate -tAq -c \
  "SELECT count(*) || '|' || count(DISTINCT rolname) FROM pg_roles WHERE rolname IN ('cmslearn_generator', 'cmslearn_certifier')")"
[ "$roles_present" = "2|2" ] || fail "the generator and the certifier are not two distinct roles: $roles_present"
note "the generator and the certifier are two distinct roles"

as_certifier() {
  psql -X -h "$work" -p "$port" -U postgres -d pipeline_gate -tAq \
    -c "SET ROLE cmslearn_certifier" -c "$1" 2>&1
}

# A verification with no certificate is refused whoever asks, because the certificate is what a later reader reads.
refusal="$(psql -X -h "$work" -p "$port" -U postgres -d pipeline_gate -tAq \
  -c "UPDATE tasks SET verification = 'verified'" 2>&1 || true)"
printf '%s' "$refusal" | grep -q "has no certificate for verified" \
  || fail "a verification without a certificate was not refused: $refusal"
note "a verification without its certificate is refused, naming the certificate"

# The insert path is guarded too, so the rule is a property of the table and not of the statement a caller happens to
# use. A row cannot be created verified: the certificate's foreign key names a task row, so none can exist yet.
refusal="$(psql -X -h "$work" -p "$port" -U postgres -d pipeline_gate -tAq \
  -c "INSERT INTO tasks (id, version, status, title_en, title_th, difficulty, time_limit_ms, memory_limit_mb, checker, origin, verification, solution, content_hash) VALUES ('insert-probe', 1, 'draft', 'Insert probe', 'ทดสอบ', 900, 1000, 256, 'token', 'original', 'verified', '', '')" 2>&1 || true)"
printf '%s' "$refusal" | grep -q "has no certificate for verified" \
  || fail "an INSERT of a verified row without a certificate was not refused: $refusal"
note "an insert of a verified row without its certificate is refused, naming the certificate"

# The start state is a property of the table too: a job is created queued, and a row that is already done or failed cannot
# be written at all — as the owner or as the role the worker runs under, so the rule is about the statement, not the caller.
for state in done failed; do
  refusal="$(psql -X -h "$work" -p "$port" -U postgres -d pipeline_gate -tAq \
    -c "INSERT INTO generation_jobs (task_id, task_version, state) VALUES ('probe-task', 1, '$state')" 2>&1 || true)"
  printf '%s' "$refusal" | grep -q "is created queued, not $state" \
    || fail "an insert of a $state job as the owner was not refused: $refusal"
  refusal="$(as_generator "INSERT INTO generation_jobs (task_id, task_version, state) VALUES ('probe-task', 1, '$state')" || true)"
  printf '%s' "$refusal" | grep -q "is created queued, not $state" \
    || fail "an insert of a $state job as the generator was not refused: $refusal"
  note "an insert of a $state job is refused as the owner and as the generator, naming the start state"
done

# The job side carries the same rule: a move to done needs a verified certificate for the job's own task and version,
# and the table is what refuses, not the pipeline that happens to call it first (ADR 0046 §2, ADR 0050).
psql -X -h "$work" -p "$port" -U postgres -d pipeline_gate -tAq \
  -c "UPDATE generation_jobs SET state = 'running', claimed_by = 'gate', claimed_at = now(), lease_expires_at = now() + interval '5 minutes' WHERE task_id = 'probe-task'" >/dev/null 2>&1
psql -X -h "$work" -p "$port" -U postgres -d pipeline_gate -tAq \
  -c "UPDATE generation_jobs SET state = 'awaiting-evidence' WHERE task_id = 'probe-task'" >/dev/null 2>&1
refusal="$(psql -X -h "$work" -p "$port" -U postgres -d pipeline_gate -tAq \
  -c "UPDATE generation_jobs SET state = 'done' WHERE task_id = 'probe-task'" 2>&1 || true)"
printf '%s' "$refusal" | grep -q "has no verified certificate" \
  || fail "a job moved to done without a certificate was not refused: $refusal"
note "a job with no verified certificate cannot move to done, naming the certificate"

refusal="$(as_certifier "UPDATE tasks SET title_en = 'rewritten'" || true)"
printf '%s' "$refusal" | grep -q "permission denied for table tasks" \
  || fail "the certifier rewrote a task's content: $refusal"
note "the certifier cannot write a task's content: permission denied for table tasks"

as_certifier "INSERT INTO generation_certificates (job_id, task_id, task_version, outcome, checks, commit_id, evidence) SELECT id, 'probe-task', 1, 'verified', '[\"step 3, 5 and 6 read back\"]'::jsonb, 'deadbeef', '{}'::jsonb FROM generation_jobs WHERE task_id = 'probe-task'" >/dev/null 2>&1 \
  || fail "the certifier cannot write a certificate"
wrote="$(as_certifier "UPDATE tasks SET verification = 'verified' WHERE id = 'probe-task'" || true)"
[ -z "$wrote" ] || fail "the certifier could not move the verification: $wrote"
row="$(psql -X -h "$work" -p "$port" -U postgres -d pipeline_gate -tAq -c \
  "SELECT verification FROM tasks WHERE id = 'probe-task'")"
certificates="$(psql -X -h "$work" -p "$port" -U postgres -d pipeline_gate -tAq -c \
  "SELECT count(*) FROM generation_certificates")"
[ "$row" = "verified" ] && [ "$certificates" = "1" ] \
  || fail "the certificate and the verification disagree: $row / $certificates certificate(s)"
note "the certifier writes the certificate and the verification together"

# With the certificate in place the same direct move is allowed: the guard is about the certificate, not about who asks.
wrote="$(psql -X -h "$work" -p "$port" -U postgres -d pipeline_gate -tAq \
  -c "UPDATE generation_jobs SET state = 'done' WHERE task_id = 'probe-task'" 2>&1 || true)"
[ -z "$wrote" ] || fail "a job with its certificate could not move to done: $wrote"
state="$(psql -X -h "$work" -p "$port" -U postgres -d pipeline_gate -tAq -c \
  "SELECT state FROM generation_jobs WHERE task_id = 'probe-task'")"
[ "$state" = "done" ] || fail "the job did not reach done after its certificate: $state"
note "a job with its verified certificate moves to done"

printf 'pipeline gate: OK\n'
