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
for key in CMSLEARN_MODEL_ENDPOINT CMSLEARN_MODEL_ID CMSLEARN_MODEL_KEY CMSLEARN_MODEL_PROMPT_VERSION CMSLEARN_DATABASE_URL; do
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

printf 'pipeline gate: OK\n'
