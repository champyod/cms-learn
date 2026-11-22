#!/usr/bin/env bash
# Deployment gate.
#
# The compose file is a security contract as much as a description of services, so it is checked the
# same way the content is: by asserting the invariants that make it safe, and by proving the check
# fails when one is broken (tools/falsify/cases/deploy.ts).
#
# What is asserted:
#   1. nothing but the proxy publishes a port on a public interface
#   2. the operator-only surfaces (database, dashboards) are bound to loopback
#   3. the `internal` network really has no gateway
#   4. the judge is attached to internal networks only — a submission must not reach the internet
#   5. every image is pinned to a version or digest, never `latest`
#   6. no service runs with privilege escalation enabled (the judge is the documented exception)
#   7. the Caddyfile the proxy would run is accepted by the proxy binary that will run it
#   8. /admin is routed to the panel listener the host file names, and nothing else is
#   9. the api image carries what it renders: the only thing mounted into it is its host file
#  10. the Prometheus config is valid for the Prometheus version pinned in compose
#  11. every service has a read-only root filesystem, drops every capability, adds back exactly the
#      capabilities its own job needs, sets no-new-privileges, and has memory, CPU and process limits
#  12. every writable path inside a container is a named volume or a bounded tmpfs, and every bind
#      mount is read-only except the judge's cgroup tree
#  13. the proxy binary carries no file capability, which a dropped bounding set would turn into an
#      exec failure
#  14. every image release.toml calls built has a service with a build section naming its context, and
#      compose actually builds one — a built image with no build block is silently never made
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
deploy="${DEPLOY_DIR:-$repo/deploy}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

fail() { printf 'deploy gate FAILED: %s\n' "$*" >&2; exit 1; }
note() { printf '  %s\n' "$*"; }

command -v docker >/dev/null || fail "docker is required (the gate validates the real config)"
[ -f "$deploy/docker-compose.yml" ] || fail "no docker-compose.yml in $deploy"

# ── build a throwaway .env: the compose file refuses to interpolate without required values, and
# that refusal is itself part of what makes the stack safe.
envfile="$work/test.env"
python3 - "$deploy/.env.example" "$envfile" <<'PY'
import re, sys
src, dst = sys.argv[1], sys.argv[2]
vals = {
    "DOMAIN": "learn.example.com",
    "ACME_EMAIL": "ops@example.com",
    "TAG": "test",
    "POSTGRES_PASSWORD": "gate-only-password",
    "GRAFANA_PASSWORD": "gate-only-grafana",
    "CROWDSEC_API_KEY": "gate-only-key",
    "AGE_PUBLIC_KEY": "age1gateonlygateonlygateonlygateonlygateonlygateonly",
    # The gate's throwaway .env, never a tracked file: the stack must refuse without these, so the gate
    # supplies values to prove it starts once they exist.
    "CMSLEARN_S3_ACCESS_KEY_ID": "gate-only-access-key",
    "CMSLEARN_S3_SECRET_ACCESS_KEY": "gate-only-secret-key",
}
out = []
for line in open(src):
    m = re.match(r"^([A-Z0-9_]+)=", line)
    out.append(f"{m.group(1)}={vals[m.group(1)]}\n" if m and m.group(1) in vals else line)
open(dst, "w").writelines(out)
PY

compose() {
  docker compose --profile migrate --env-file "$envfile" -f "$deploy/docker-compose.yml" "$@"
}

note "interpolation + structure"
compose config -q || fail "compose config is invalid"

note "security invariants"
compose config --format json > "$work/config.json"
python3 - "$work/config.json" <<'PY' || exit 1
import ipaddress, json, sys

cfg = json.load(open(sys.argv[1]))
services = cfg["services"]
errors = []
LOOPBACK = {"127.0.0.1", "::1"}

def published(svc):
    out = []
    for p in svc.get("ports", []):
        host_ip = p.get("host_ip") or "0.0.0.0"
        try:
            public = not ipaddress.ip_address(host_ip).is_loopback
        except ValueError:
            public = True
        out.append((host_ip, int(p.get("published", 0)), int(p.get("target", 0)), public))
    return out

# 1 + 2: port exposure
for name, svc in services.items():
    for host_ip, pub, target, public in published(svc):
        if name == "proxy":
            if pub not in (80, 443):
                errors.append(f"{name} publishes unexpected port {pub}")
            continue
        if public:
            errors.append(f"{name} publishes {pub}->{target} on {host_ip} (only proxy may face the internet)")
        elif host_ip not in LOOPBACK:
            errors.append(f"{name} publishes {pub}->{target} on {host_ip} (expected loopback)")

# 3: the internal network must have no gateway
networks = cfg.get("networks", {})
internal = networks.get("internal", {})
if not internal.get("internal"):
    errors.append("network 'internal' is not marked internal: true — containers would have egress")

# 4: judge must sit on internal networks only
judge_nets = list(services.get("judge", {}).get("networks", {}).keys())
for n in judge_nets:
    if not networks.get(n, {}).get("internal"):
        errors.append(f"judge is attached to non-internal network '{n}' — a submission could reach the internet")
if not judge_nets:
    errors.append("judge has no network defined")

# 5: pinning
for name, svc in services.items():
    image = svc.get("image", "")
    if not image:
        continue
    tag = image.rsplit(":", 1)[-1] if ":" in image.rsplit("/", 1)[-1] else "<none>"
    if tag in ("latest", "<none>") and "@sha256:" not in image:
        errors.append(f"{name} image is not pinned: {image}")

# 6, 11 + 12: every container is hardened. The two tables below are the contract, not a description of the
# file: a service missing from a table fails as loudly as one whose row changed, so a new service cannot join
# the stack without someone deciding why it needs what it asks for. ADR 0060 carries the reasoning per row.
CAP_ADD = {
    "api": set(),
    "backup": set(),
    "crowdsec": set(),
    "database": {"CHOWN", "DAC_OVERRIDE", "SETGID", "SETUID"},
    "grafana": set(),
    "judge": {"CHOWN", "DAC_OVERRIDE", "FOWNER", "NET_ADMIN", "SETGID", "SETUID", "SYS_ADMIN", "SYS_CHROOT"},
    "node-exporter": set(),
    "objects": {"SETGID", "SETUID"},
    "prometheus": set(),
    "proxy": set(),
    "schema": set(),
}
WRITABLE_PATHS = {
    "api": {"/tmp"},
    "backup": {"/backups", "/tmp"},
    "crowdsec": {"/etc/crowdsec", "/tmp", "/var/lib/crowdsec/data"},
    "database": {"/tmp", "/var/lib/postgresql/data", "/var/run/postgresql"},
    "grafana": {"/tmp", "/var/lib/grafana"},
    "judge": {"/run/isolate", "/sys/fs/cgroup", "/tmp", "/var/lib/cms-learn/work", "/var/lib/isolate"},
    "node-exporter": set(),
    "objects": {"/data", "/tmp"},
    "prometheus": {"/prometheus", "/tmp"},
    "proxy": {"/config", "/data", "/tmp", "/var/log/caddy"},
    "schema": {"/tmp"},
}

for name in sorted(set(services) - set(CAP_ADD)):
    errors.append(f"{name} has no row in the hardening table — a service cannot join the stack unexplained")
for name in sorted(set(CAP_ADD) - set(services)):
    errors.append(f"the hardening table names {name}, which no service defines")

for name, svc in services.items():
    if name not in CAP_ADD:
        continue
    if svc.get("read_only") is not True:
        errors.append(f"{name} runs on a writable root filesystem; read_only: true is the rule")
    dropped = [str(cap).upper() for cap in (svc.get("cap_drop") or [])]
    if dropped != ["ALL"]:
        errors.append(f"{name} does not drop every capability: cap_drop is {dropped or 'unset'}")
    added = {str(cap).upper() for cap in (svc.get("cap_add") or [])}
    if added != CAP_ADD[name]:
        errors.append(
            f"{name} adds back {', '.join(sorted(added)) or 'nothing'} but its job needs "
            f"{', '.join(sorted(CAP_ADD[name])) or 'nothing'}"
        )
    options = [str(option) for option in (svc.get("security_opt") or [])]
    if "no-new-privileges:true" not in options:
        errors.append(f"{name} does not set no-new-privileges:true")
    loosens_seccomp = any(option.startswith("seccomp:") for option in options)
    if name == "judge" and "seccomp:unconfined" not in options:
        errors.append("judge does not document its seccomp exception")
    if name != "judge" and loosens_seccomp:
        errors.append(f"{name} loosens seccomp; only the judge's sandbox has a reason")
    limits = ((svc.get("deploy") or {}).get("resources") or {}).get("limits") or {}
    for field, label in (("memory", "memory"), ("cpus", "CPU"), ("pids", "process")):
        try:
            amount = float(limits.get(field))
        except (TypeError, ValueError):
            amount = 0
        if amount <= 0:
            errors.append(f"{name} sets no {label} limit")
    writable = set()
    for volume in svc.get("volumes") or []:
        if not volume.get("read_only"):
            writable.add(str(volume.get("target")))
    for mount in svc.get("tmpfs") or []:
        writable.add(str(mount).split(":", 1)[0])
    if writable != WRITABLE_PATHS[name]:
        errors.append(
            f"{name}'s writable paths are {sorted(writable) or 'none'}, but the container writes "
            f"{sorted(WRITABLE_PATHS[name]) or 'nowhere'} — every path it writes must be a volume or a tmpfs, "
            "and every bind mount must be read-only"
        )

# 7: the api carries what it renders. The one bind mount it may have is the operator's own configuration; a
# template, a stylesheet or the settings catalogue inside the container must come from the image, or the thing
# serving the page is a checkout on the host rather than the artifact that was reviewed.
HOST_FILE_TARGET = "/run/cms-learn/host.env"
for volume in services.get("api", {}).get("volumes", []):
    if volume.get("type") == "bind" and volume.get("target") != HOST_FILE_TARGET:
        errors.append(
            f"api mounts {volume.get('source')} at {volume.get('target')}; "
            "the image must carry what it renders"
        )

if errors:
    print("deploy gate FAILED:", file=sys.stderr)
    for e in errors:
        print(f"  - {e}", file=sys.stderr)
    sys.exit(1)
print(f"  {len(services)} services: exposure, isolation and hardening invariants hold")
PY

# 14: the release file is the list. A service whose image release.toml calls built must carry the build
# section, or compose exits 0 having built nothing and the run pulls an image that does not exist.
python3 - "$repo/release.toml" "$deploy/docker-compose.yml" <<'PY'
import sys
import tomllib

import yaml

release = tomllib.load(open(sys.argv[1], "rb"))
services = yaml.safe_load(open(sys.argv[2])).get("services", {})
built = {key: entry for key, entry in release.get("images", {}).items() if entry.get("kind") == "built"}
errors = []
for key, entry in sorted(built.items()):
    wanted = f"{entry['ref']}:${{TAG:-dev}}"
    matching = [name for name, service in services.items() if service.get("image") == wanted]
    if not matching:
        errors.append(f"built image {key} ({wanted}) is used by no service")
        continue
    for name in matching:
        build = services[name].get("build")
        if not isinstance(build, dict) or not build.get("context"):
            errors.append(
                f"service {name} runs built image {key} without a build section — compose build would produce nothing"
            )
            continue
        context = str(build["context"]).rstrip("/").split("/")[-1]
        if context != entry.get("context"):
            errors.append(
                f"service {name} builds from {build['context']}, but release.toml builds {key} from {entry.get('context')}"
            )
if errors:
    print("deploy gate FAILED:", file=sys.stderr)
    for error in errors:
        print(f"  - {error}", file=sys.stderr)
    sys.exit(1)
print(f"  {len(built)} built image(s) each have a matching build section")
PY

note "caddy validates the Caddyfile with the modules it will run"
proxy_image="cms-learn-proxy:gate"
docker build -q -t "$proxy_image" "$deploy/proxy" >/dev/null || fail "proxy image did not build"

# The build section is only worth asserting if compose acts on it: the schema image is built through the
# rendered stack, and the tag the release names must exist afterwards.
schema_ref="$(python3 -c 'import sys,tomllib; print(tomllib.load(open(sys.argv[1], "rb"))["images"]["schema"]["ref"])' "$repo/release.toml")"
compose build schema >/dev/null || fail "compose build schema failed"
docker image inspect "${schema_ref}:test" >/dev/null 2>&1 \
  || fail "compose build schema produced no image at ${schema_ref}:test"
note "compose build schema produced ${schema_ref}:test"

# 13: a file capability is not in the container's bounding set once every capability is dropped, and the
# kernel refuses to exec a binary carrying one outside it. The proxy's job needs no capability, so the
# binary must carry none either; a green compose cannot save a container that cannot start.
file_caps="$(docker run --rm --entrypoint /usr/sbin/getcap "$proxy_image" /usr/bin/caddy)" \
  || fail "could not read the proxy binary's file capabilities"
[ -z "$file_caps" ] \
  || fail "the proxy binary carries file capabilities ($file_caps); with cap_drop: [ALL] it cannot be executed"
note "the proxy binary carries no file capability"

docker run --rm --entrypoint caddy \
  -e DOMAIN=learn.example.com -e ACME_EMAIL=ops@example.com -e CROWDSEC_API_KEY=gate-only-key \
  -v "$deploy/Caddyfile:/etc/caddy/Caddyfile:ro" "$proxy_image" \
  validate --config /etc/caddy/Caddyfile --adapter caddyfile >/dev/null 2>"$work/caddy.err" \
  || { cat "$work/caddy.err" >&2; fail "Caddyfile is not valid"; }

note "the panel route is asserted, not merely valid"
docker run --rm --entrypoint caddy \
  -e DOMAIN=learn.example.com -e ACME_EMAIL=ops@example.com -e CROWDSEC_API_KEY=gate-only-key \
  -v "$deploy/Caddyfile:/etc/caddy/Caddyfile:ro" "$proxy_image" \
  adapt --config /etc/caddy/Caddyfile --adapter caddyfile >"$work/caddy.json" \
  || fail "Caddyfile could not be adapted"

python3 - "$work/caddy.json" "$deploy/host.env.example" "$work/config.json" <<'PANEL' || exit 1
import json, re, sys

adapted = json.load(open(sys.argv[1]))
example_path, config_path = sys.argv[2], sys.argv[3]

# The proxy and the panel listener are two files that must name one number, and the host file is where the
# listener's number is written, so the route is checked against it rather than against a literal here.
panel_port = None
for line in open(example_path):
    match = re.match(r"^CMSLEARN_PANEL_LISTEN_PORT=([0-9]+)$", line.strip())
    if match:
        panel_port = int(match.group(1))
if panel_port is None:
    print("deploy gate FAILED: deploy/host.env.example names no CMSLEARN_PANEL_LISTEN_PORT", file=sys.stderr)
    sys.exit(1)

handlers = []


def walk(node, paths):
    if isinstance(node, dict):
        matched = paths
        for matcher in node.get("match") or []:
            if isinstance(matcher, dict) and isinstance(matcher.get("path"), list):
                matched = matched + list(matcher["path"])
        if node.get("handler") == "reverse_proxy":
            handlers.append((matched, [up.get("dial") for up in node.get("upstreams") or []]))
        for value in node.values():
            walk(value, matched)
    elif isinstance(node, list):
        for item in node:
            walk(item, paths)


walk(adapted, [])

panel = [dial for paths, dials in handlers if "/admin" in paths or "/admin/*" in paths for dial in dials]
public = [dial for paths, dials in handlers if not paths for dial in dials]

errors = []
if not panel:
    errors.append("/admin is not routed to the panel listener: deploy/Caddyfile has no /admin handler")
elif panel != [f"api:{panel_port}"]:
    errors.append(
        "/admin is not routed to the panel listener: it reaches "
        + ", ".join(panel)
        + f", while deploy/host.env.example names CMSLEARN_PANEL_LISTEN_PORT={panel_port}"
    )
if not public:
    errors.append("no catch-all route serves the site")
elif f"api:{panel_port}" in public:
    errors.append(f"the catch-all and /admin share api:{panel_port}; the panel needs a listener of its own")

exposed = {
    int(port.split("/")[0])
    for port in json.load(open(config_path))["services"]["api"].get("expose", [])
}
if panel_port not in exposed:
    errors.append(f"api does not expose {panel_port}, the panel port the host file names")

if errors:
    print("deploy gate FAILED:", file=sys.stderr)
    for error in errors:
        print(f"  - {error}", file=sys.stderr)
    sys.exit(1)
print(f"  /admin reaches the panel listener on api:{panel_port}; the catch-all keeps the site on api:8080")
PANEL

note "prometheus config parses for the pinned version"
prom_image="$(python3 -c "import json;print(json.load(open('$work/config.json'))['services']['prometheus']['image'])")"
docker run --rm -v "$deploy/prometheus/prometheus.yml:/etc/prometheus/prometheus.yml:ro" \
  --entrypoint promtool "$prom_image" check config /etc/prometheus/prometheus.yml >/dev/null \
  || fail "prometheus config is invalid"

# ── 11. the object store has a credential, and no tracked file carries one ──────────────────────
# The bytes of a task live in the store and the network is no longer the only boundary (ADR 0023), so the
# stack must refuse to start without the credential and the tracked examples must name it empty.
for key in CMSLEARN_S3_ACCESS_KEY_ID CMSLEARN_S3_SECRET_ACCESS_KEY; do
  grep -q "^$key=$" "$deploy/.env.example" || fail "deploy/.env.example must name $key empty"
  grep -q "\${$key:?" "$deploy/docker-compose.yml" || fail "docker-compose.yml does not require $key"
done
python3 - "$work/config.json" <<'PY' || exit 1
import json, sys
service = json.load(open(sys.argv[1]))["services"]["objects"]
environment = service.get("environment", {})
for name in ("AWS_ACCESS_KEY_ID", "AWS_SECRET_ACCESS_KEY"):
    if not environment.get(name):
        raise SystemExit(f"deploy gate FAILED: objects does not take {name} from .env")
PY
note "the object store takes its credential from .env, and the compose requires it"

if git -C "$repo" ls-files -z | xargs -0 grep -lnE "AKIA[0-9A-Z]{16}" 2>/dev/null | grep -q .; then
  fail "a tracked file carries what looks like a store credential"
fi
note "no tracked file carries a store credential"

# ── 11b. the migrate step reads what it runs ──────────────────────────────────────────────────────
# t1's schema container mounted deploy/plan and tools but not docs/dependencies.md, so the pin the wrapper
# exists to enforce could not be read and the migrate failed inside pgschema with a path that did not exist.
python3 - "$work/config.json" "$repo" "$deploy" <<'PY' || exit 1
import json, os, sys
config, repo, deploy = sys.argv[1], sys.argv[2], sys.argv[3]
services = json.load(open(config))["services"]
schema = services.get("schema")
if not schema:
    raise SystemExit("deploy gate FAILED: the stack has no schema service")
wanted = {
    "/app/deploy/plan": ("deploy", "plan"),
    "/app/tools": ("tools",),
    "/app/docs/dependencies.md": ("docs", "dependencies.md"),
}
mounted = {str(volume.get("target")): volume for volume in (schema.get("volumes") or [])}
for target, suffix in wanted.items():
    volume = mounted.get(target)
    if volume is None:
        raise SystemExit(f"deploy gate FAILED: the schema service does not mount {target}; the migrate step cannot read what it runs")
    if volume.get("read_only") is not True:
        raise SystemExit(f"deploy gate FAILED: the schema service mounts {target} writable")
    source = str(volume.get("source", "")).replace("\\", "/")
    if not source.endswith("/".join(suffix)):
        raise SystemExit(f"deploy gate FAILED: the schema service mounts {target} from {source}")
    if os.path.realpath(deploy) == os.path.realpath(os.path.join(repo, "deploy")) and not os.path.exists(source):
        raise SystemExit(f"deploy gate FAILED: the schema service mounts {target} from {source}, which does not exist")
print("  the migrate step mounts the plan, the tools and the pin it reads")
PY

if CMSLEARN_PGSCHEMA_DOC="$work/absent-dependencies.md" bash -c '. "$1/tools/pinned-pgschema.sh"' _ "$repo" >"$work/refusal" 2>&1; then
  fail "the pin resolver accepted a tree without its pin document"
fi
grep -q "absent-dependencies.md" "$work/refusal" || fail "the pin resolver did not name the missing pin document"
note "a missing pin document is refused by name"

# ── 12. every service has its own role, and the separation is attempted rather than declared ──────
# ADR 0058: the grants were written but nothing enforced them, because every service connected as the same user. These
# attempts use each service's own credentials, and every line names the role that made it, so a refusal for the web cannot
# be mistaken for a refusal from a superuser.
python3 - "$work/config.json" "$deploy/host.env.example" <<'PY' || exit 1
import json, sys
services = json.load(open(sys.argv[1]))["services"]
example = open(sys.argv[2]).read()

def role_of(url, name, role):
    if role not in url:
        raise SystemExit(f"deploy gate FAILED: {name} does not connect as {role}: {url!r}")
    if ":" in url.split("@")[0].split("//")[-1]:
        raise SystemExit(f"deploy gate FAILED: {name}'s database URL carries a password")

# The api takes every fact from its host file, so its role is asserted where that file declares it.
for line in example.splitlines():
    if line.startswith("CMSLEARN_DATABASE_URL="):
        role_of(line.split("=", 1)[1], "api", "cmslearn_api_user")
        break
else:
    raise SystemExit("deploy gate FAILED: the host file names no database URL")
role_of((services["judge"].get("environment") or {}).get("DATABASE_URL", ""), "judge", "cmslearn_judge_user")

for name in ("api", "judge"):
    environment = services[name].get("environment") or {}
    if not environment.get("PGPASSFILE"):
        raise SystemExit(f"deploy gate FAILED: {name} does not name the file its password comes from")
    if not services[name].get("secrets"):
        raise SystemExit(f"deploy gate FAILED: {name} mounts no secret")
print("  every service names its own role, and its password file is the only place the credential lives")
PY

attempt() {  # role, password, SQL: prints the role beside the outcome, then the outcome alone
  local role="$1" password="$2" sql="$3" outcome
  if PGPASSWORD="$password" PGHOST="$cluster" PGPORT="$port" PGDATABASE=cmslearn_roles PGUSER="$role"             psql -X -q -tA -c "$sql" >/dev/null 2>"$work/attempt.err"; then
    outcome="allowed"
  else
    outcome="refused: $(head -1 "$work/attempt.err" | sed 's/^ERROR:  //')"
  fi
  printf '  %-22s %s\n' "$role" "$outcome" >&2
  printf '%s' "$outcome"
}

expect_refused() {
  local got
  got="$(attempt "$1" "$2" "$3")"
  case "$got" in
    refused:*) note "$1 is refused $4" ;;
    *) fail "$1 was allowed $4 — a service's role reaches what it does not need" ;;
  esac
}

expect_allowed() {
  local got
  got="$(attempt "$1" "$2" "$3")"
  [ "$got" = "allowed" ] || fail "$1 cannot $4, which its job needs ($got)"
  note "$1 may $4"
}

CERTIFICATE_INSERT="INSERT INTO generation_certificates (job_id, task_id, task_version, outcome, checks, commit_id, evidence) VALUES (NULL, 'gate', 1, 'verified', '[\"the deploy gate wrote this\"]'::jsonb, repeat('a', 40), '{}'::jsonb)"

note "the first-run target exists and does what it claims"
makefile="${FIRST_RUN_MAKEFILE:-$repo/Makefile}"
[ -f "$makefile" ] || fail "no Makefile at $makefile"
grep -qE '^first-run:' "$makefile" || fail "the Makefile declares no first-run target"
grep -qE '^[[:space:]]+bash tools/first-run\.sh$' "$makefile" || fail "the first-run target does not run tools/first-run.sh"
[ -f "$repo/tools/first-run.sh" ] || fail "no tools/first-run.sh"
bash "$repo/tools/check-first-run.sh" || fail "the first-run target does not do what it claims"

if command -v initdb >/dev/null 2>&1 && command -v psql >/dev/null 2>&1; then
  . "$repo/tools/postgres-cluster.sh"
  pg_cluster_binaries
  cluster="$(mktemp -d)"
  port=$((56400 + (RANDOM % 200)))
  pg_cluster_prepare "$cluster" >/dev/null 2>&1
  pg_cluster_init "$cluster" >"$cluster/initdb.log" 2>&1 || fail "the scratch cluster would not start"
  pg_cluster_start "$cluster" "$port" "$cluster/pg.log" >/dev/null 2>&1 || fail "the scratch cluster would not start"
  trap 'pg_cluster_stop "$cluster" >/dev/null 2>&1 || true; rm -rf "$cluster"' EXIT

  psql -X -h "$cluster" -p "$port" -U postgres -q -c "CREATE DATABASE cmslearn_roles" >/dev/null 2>&1
  psql -X -h "$cluster" -p "$port" -U postgres -d cmslearn_roles -q -v ON_ERROR_STOP=1     -v api_password=gate_api -v generator_password=gate_generator -v certifier_password=gate_certifier     -v judge_password=gate_judge -v bank_password=gate_bank -v backup_password=gate_backup     -f "$deploy/roles.sql" >/dev/null 2>&1 || fail "deploy/roles.sql does not apply"
  PGHOST="$cluster" PGPORT="$port" PGDATABASE=cmslearn_roles PGUSER=postgres     bash "$repo/tools/apply-plan.sh" >/dev/null 2>&1 || fail "the plan does not apply to the roles database"

  # A certificate names a task, so the attempts below need one row to point at; it is written as the owner, because the
  # point of these attempts is what the service roles may do afterwards, not how the row got there.
  psql -X -h "$cluster" -p "$port" -U postgres -d cmslearn_roles -q >/dev/null 2>&1 <<'SQL'
SELECT set_config('cms.actor_id', 'a0000001-1111-4111-8111-0000000000d1', true);
SELECT set_config('cms.actor_kind', 'admin', true);
SELECT set_config('cms.invoked_by', 'check-deploy', true);
INSERT INTO admin_accounts (id, handle, status) VALUES ('a0000001-1111-4111-8111-0000000000d1', 'gate', 'active')
  ON CONFLICT DO NOTHING;
INSERT INTO tasks (id, version, status, title_en, title_th, difficulty, time_limit_ms, memory_used_limit_mb, memory_reserved_limit_mb, checker,
                   origin, verification, solution, content_hash)
VALUES ('gate-task', 1, 'draft', 'Gate', 'ประตู', 900, 1000, 256, 512, 'token', 'original', 'pending', '', repeat('f', 64))
  ON CONFLICT DO NOTHING;
SQL

  expect_allowed cmslearn_certifier_user gate_certifier "${CERTIFICATE_INSERT/gate/gate-task}" "write a certificate"
  expect_refused cmslearn_certifier_user gate_certifier     "INSERT INTO generation_jobs (task_id, task_version) VALUES ('gate-task', 1)" "a generation job"
  expect_refused cmslearn_certifier_user gate_certifier     "UPDATE tasks SET title_en = 'hijacked' WHERE id = 'gate-task'" "any column of a task but its verification"
  expect_refused cmslearn_generator_user gate_generator "$CERTIFICATE_INSERT" "a certificate"
  expect_refused cmslearn_judge_user gate_judge "SELECT * FROM learner_accounts" "the bank"
  expect_refused cmslearn_api_user gate_api "$CERTIFICATE_INSERT" "a certificate"
  note "each line above names the role that attempted it, so no refusal came from a superuser"
else
  note "skipped: no PostgreSQL binaries, so the service roles were not attempted"
fi

printf 'deploy gate: OK\n'
