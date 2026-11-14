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

# Debian and Ubuntu put the server binaries under /usr/lib/postgresql/<version>/bin, which is not on PATH;
# the gate finds them rather than reporting a gate that could not run for want of a file that exists.
if ! command -v initdb >/dev/null 2>&1 || ! command -v pg_ctl >/dev/null 2>&1; then
  pgdir="$(printf '%s\n' /usr/lib/postgresql/*/bin | sort | tail -1)"
  if [ -x "$pgdir/initdb" ]; then
    PATH="$pgdir:$PATH"
  fi
fi

work="$(mktemp -d)"

# PostgreSQL refuses to run as root, and a CI container's steps run as root: the cluster belongs to the
# unprivileged postgres user, and only the server commands are handed to it.
pg_user="$(id -un)"
if [ "$(id -u)" -eq 0 ]; then
  pg_user=postgres
  id -u "$pg_user" >/dev/null 2>&1 || fail "the gate runs as root and there is no '$pg_user' user to own the cluster"
  command -v runuser >/dev/null 2>&1 || fail "the gate runs as root and runuser is not installed"
  chown "$pg_user" "$work"
fi

run_pg() {
  if [ "$pg_user" != "$(id -un)" ]; then
    runuser -u "$pg_user" -- "$@"
  else
    "$@"
  fi
}

cleanup() {
  run_pg pg_ctl -D "$work/data" -m immediate stop >/dev/null 2>&1 || true
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
for tool in initdb pg_ctl psql createdb; do
  command -v "$tool" >/dev/null 2>&1 || {
    note "the role half did not run: PostgreSQL binaries are not installed ($tool is missing)"
    fail "the role half of the pipeline gate did not run; a gate that passes without it proves nothing"
  }
done
run_pg initdb -D "$work/data" -U postgres -A trust >/dev/null 2>&1
run_pg pg_ctl -D "$work/data" -o "-c listen_addresses='' -c unix_socket_directories=$work -p $port" \
  -l "$work/pg.log" start >/dev/null 2>&1
sleep 1
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
