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
#  15. the upgrade's switch waits for the release's services, not for the backup sidecar, whose first
#      dump has not happened yet on a fresh deployment
#  16. the edge's AppSec listener is wired: the datasource, its config and the remediation profile are
#      mounted, the CRS rules are installed, and the Caddyfile's appsec_url port is the one the
#      datasource listens on — a mismatch is a finding, never a silent fallback
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
        declared = str(entry.get("context", "")).rstrip("/").split("/")[-1]
        if context != declared:
            errors.append(
                f"service {name} builds from {build['context']}, but release.toml builds {key} from {entry.get('context')}"
            )
        if entry.get("dockerfile") is not None and build.get("dockerfile") != entry["dockerfile"]:
            errors.append(
                f"service {name} builds from {build.get('dockerfile')}, but release.toml names {entry['dockerfile']}"
            )

built_refs = {f"{entry['ref']}:${{TAG:-dev}}": key for key, entry in built.items()}
for name, service in services.items():
    if service.get("build") is not None and service.get("image") not in built_refs:
        errors.append(f"service {name} has a build section but release.toml does not call its image built")
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

# 15: a first deployment must finish its wait. The backup sidecar has no archive to report until its
# first window, so the switch waits for the services it serves and not for that one.
grep -qE 'compose up -d --wait \$wait_for' "$repo/tools/upgrade.sh" \
  || fail "the switch does not wait on the services it names"
grep -qE 'grep -vxE .backup\|schema.' "$repo/tools/upgrade.sh" \
  || fail "the wait does not exclude the backup sidecar"
if grep -qE 'up -d --wait(\s*\\|\s*\|\||\s*$)' "$repo/tools/upgrade.sh"; then
  fail "the switch still waits on every service, including one with nothing to report yet"
fi
note "the upgrade wait excludes the backup sidecar"

# The image's HEALTHCHECK is exec-form. A script without a shebang cannot be exec'd at all, so the
# service is unhealthy forever — on every deployment, with nothing to report and no way to say so.
head -1 "$deploy/backup/health.sh" | grep -q '^#!' \
  || fail "deploy/backup/health.sh has no shebang; the exec-form healthcheck fails with exec format error"
grep -q 'CMD \["/usr/local/bin/health.sh"\]' "$deploy/backup/Dockerfile" \
  || fail "the backup healthcheck no longer execs health.sh; the gate's contract moved"
note "the backup healthcheck is an executable script with a fresh-install answer"

python3 - "$deploy" <<'PY'
import re
import sys

import yaml

deploy = sys.argv[1]
compose = yaml.safe_load(open(f"{deploy}/docker-compose.yml"))
crowdsec = compose["services"]["crowdsec"]


def target_of(fragment):
    for volume in crowdsec.get("volumes", []):
        source, target = (volume.get("source", ""), volume.get("target", "")) if isinstance(volume, dict) else volume.split(":", 2)[:2]
        if fragment in source:
            return target
    return None


errors = []
if target_of("crowdsec/appsec.yaml") != "/etc/crowdsec/acquis.d/appsec.yaml":
    errors.append("the AppSec datasource is not mounted where CrowdSec reads acquisition files")
if not str(target_of("crowdsec/appsec-config.yaml") or "").startswith("/etc/crowdsec/appsec-configs/"):
    errors.append("the AppSec config is not mounted under /etc/crowdsec/appsec-configs")
if target_of("crowdsec/profiles.yaml") != "/etc/crowdsec/profiles.yaml":
    errors.append("the WAF remediation profile is not mounted as CrowdSec's profiles file")
if "crowdsecurity/crs" not in str(crowdsec.get("environment", {}).get("APPSEC_RULES", "")):
    errors.append("the CRS AppSec rules are not installed by the service")

datasource = yaml.safe_load(open(f"{deploy}/crowdsec/appsec.yaml"))
listen_port = str(datasource.get("listen_addr", "")).rsplit(":", 1)[-1]
caddy = open(f"{deploy}/Caddyfile").read()
found = re.search(r"appsec_url\s+http://[^:]+:(\d+)", caddy)
if found is None:
    errors.append("the Caddyfile declares no appsec_url")
elif found.group(1) != listen_port:
    errors.append(
        f"the Caddyfile calls AppSec on port {found.group(1)}, but the datasource listens on {listen_port}"
    )
config = yaml.safe_load(open(f"{deploy}/crowdsec/appsec-config.yaml"))
if config.get("name") != datasource.get("appsec_config"):
    errors.append(
        f"the datasource loads {datasource.get('appsec_config')}, but the mounted config is named {config.get('name')}"
    )
if 'Alert.Kind == "waf"' not in open(f"{deploy}/crowdsec/profiles.yaml").read():
    errors.append("no profile turns a waf alert into a decision")

if errors:
    print("deploy gate FAILED:", file=sys.stderr)
    for error in errors:
        print(f"  - {error}", file=sys.stderr)
    sys.exit(1)
print(f"  AppSec is mounted and the Caddyfile calls its listener on port {listen_port}")
PY

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

note "the edge enforces a content policy, and the report path exists"
python3 - "$deploy/Caddyfile" "$repo/api/src/main.cpp" <<'POLICY' || exit 1
import re, sys

edge = open(sys.argv[1]).read()
api = open(sys.argv[2]).read()
header = re.search(r'Content-Security-Policy "([^"]+)"', edge)
if header is None:
    sys.exit("the edge sets no Content-Security-Policy")
policy = header.group(1)
required = ["default-src 'self'", "script-src 'self'", "frame-ancestors 'none'",
            "base-uri 'none'", "object-src 'none'", "report-uri /csp-report"]
missing = [directive for directive in required if directive not in policy]
if missing:
    sys.exit("the policy does not carry: " + ", ".join(missing))
if "Content-Security-Policy-Report-Only" in edge:
    sys.exit("the edge reports violations without blocking them")
if '"/csp-report"' not in api:
    sys.exit("the edge sends reports to /csp-report and no service registers that path")
print("  policy enforces and reports to a path the api serves")
POLICY

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

# ── 11c. the migrate applies desired state, for the owner the deployment uses ─────────────────────
# A pre-generated plan JSON is bound to the database it was rendered from and refuses every other one, so
# the migrate cannot upgrade an existing database; and a plan rendered as postgres stamps FOR ROLE postgres,
# which a fresh deployment (whose database is owned by POSTGRES_USER) fails on. Both are asserted here.
grep -qE 'pgschema_bin" apply --file .*0001_init\.sql' "$repo/tools/apply-plan.sh" \
  || fail "the migrate does not apply the desired state (apply-plan.sh must run pgschema apply --file 0001_init.sql)"
if grep -qE 'pgschema_bin" apply --plan' "$repo/tools/apply-plan.sh"; then
  fail "the migrate applies a pre-generated plan; a fingerprint-bound plan cannot upgrade another database"
fi
owner="$(sed -n 's/^POSTGRES_USER=//p' "$deploy/.env.example" | head -1)"
[ -n "$owner" ] || fail "deploy/.env.example names no POSTGRES_USER"
foreign="$(grep -oE 'FOR ROLE [a-z_]+' "$deploy/plan/0001_init.sql" | sed 's/FOR ROLE //' | sort -u | grep -vx "$owner" || true)"
[ -z "$foreign" ] \
  || fail "the plan is rendered for a foreign owner (${foreign//$'\n'/, }); the deployment owns its database as $owner"
grep -q 'POSTGRES_USER' "$repo/tools/check-schema.sh" \
  || fail "check:schema does not build its database as the deployment owner"
note "the migrate applies desired state, and the plan and the gate both use $owner"

# ── 12. every service has its own role, and the separation is attempted rather than declared ──────
# ADR 0058: the grants were written but nothing enforced them, because every service connected as the same user. These
# attempts use each service's own credentials, and every line names the role that made it, so a refusal for the web cannot
# be mistaken for a refusal from a superuser.
python3 - "$work/config.json" "$deploy/host.env.example" <<'PY' || exit 1
import json, re, sys
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
role_of(
    (services["judge"].get("environment") or {}).get("CMSLEARN_DATABASE_URL", ""),
    "judge",
    "cmslearn_judge_user",
)

# No service anywhere carries a password in its environment or in a connection URL: an environment is
# readable from docker inspect and the process list, which is what the mounted files exist to avoid.
credential_url = re.compile(r"://[^/@\s:]+:[^/@\s]+@")
for name, service in services.items():
    environment = service.get("environment") or {}
    if "PGPASSWORD" in environment:
        raise SystemExit(f"deploy gate FAILED: {name} carries a database password in the environment")
    for key, value in environment.items():
        if isinstance(value, str) and credential_url.search(value):
            raise SystemExit(f"deploy gate FAILED: {name}'s {key} carries a password in a connection URL")

# Every job that needs a database credential reads it from a mounted file the client accepts.
credential_jobs = {
    "api": ("api_db_password", "its own role"),
    "judge": ("judge_db_password", "its own role"),
    "schema": ("owner_db_password", "the owner, because the DDL is applied as the owner"),
    "backup": ("owner_db_password", "the owner, because a full dump reads every table"),
}
for name, (secret_name, why) in credential_jobs.items():
    service = services[name]
    environment = service.get("environment") or {}
    passfile = environment.get("PGPASSFILE") or ""
    if not passfile:
        raise SystemExit(f"deploy gate FAILED: {name} does not name the file its password comes from")
    entries = service.get("secrets") or []
    if not entries:
        raise SystemExit(f"deploy gate FAILED: {name} mounts no secret")
    if entries[0].get("source") != secret_name:
        raise SystemExit(f"deploy gate FAILED: {name} does not mount {secret_name}")
    # A secret mount is 0444 (0777 on a daemon that does not apply the mode), and libpq ignores a password
    # file any other user can read. The entrypoint must install it as the job's own user with mode 600.
    entrypoint = " ".join(str(part) for part in (service.get("entrypoint") or []))
    expected = f"install -m 600 /run/secrets/{secret_name} {passfile}"
    if expected not in entrypoint:
        raise SystemExit(
            f"deploy gate FAILED: {name} does not install its secret as {passfile} with mode 600; "
            "a 0444 secret is ignored by libpq"
        )
print("  no service carries a password in an environment or a URL")
print("  every credential job reads a mounted file; the owner credential is the schema (DDL) and the backup (full dump)")
PY

# ── 12b. the health signal and the drill use the database the deployment uses ────────────────────────
# A server that accepts connections is not a database the schema can use: pg_isready passed on a clean
# volume whose application database the init never created, and the migrate then failed on a stack the
# health check called ready.
python3 - "$work/config.json" "$deploy/.env.example" <<'PY' || exit 1
import json, sys
services = json.load(open(sys.argv[1]))["services"]
example = open(sys.argv[2]).read()
db = "cmslearn"
for line in example.splitlines():
    if line.startswith("POSTGRES_DB="):
        db = line.split("=", 1)[1].strip() or db
test = " ".join(str(part) for part in (services["database"].get("healthcheck") or {}).get("test") or [])
if "pg_isready" in test:
    raise SystemExit(
        "deploy gate FAILED: the database health check probes the server (pg_isready), not the "
        f"application database {db}; a stack with no {db} database would report healthy"
    )
if "psql" not in test or db not in test:
    raise SystemExit(
        f"deploy gate FAILED: the database health check does not query {db} as the application role"
    )
print(f"  the database health check queries the application database {db}")
PY
grep -q '/run/secrets/owner_db_password' "$repo/deploy/backup/restore-drill.sh" \
  || fail "the restore drill does not install the owner credential file; the documented command cannot authenticate"
grep -q 'PGPASSFILE' "$repo/deploy/backup/restore-drill.sh" \
  || fail "the restore drill does not name the password file its client reads"
note "the restore drill installs the owner credential file itself"

python3 - "$work/config.json" <<'PY' || exit 1
import json, sys
services = json.load(open(sys.argv[1]))["services"]
targets = {v.get("target") for v in services["backup"].get("volumes") or [] if isinstance(v, dict)}
for want in ("/run/cms-learn/secrets", "/run/cms-learn/roles.sql"):
    if want not in targets:
        raise SystemExit(
            f"deploy gate FAILED: the backup service does not mount {want}; "
            "the restore drill cannot recreate the roles a dump names"
        )
print("  the backup service carries the role material the drill needs")
PY

# ── 12c. a restore brings the platform back, not just the rows ──────────────────────────────────────
# On a destroyed volume the dump's CREATE POLICY and GRANT statements name roles that no longer exist,
# and a --no-privileges restore leaves the api unable to read what it restored. The drill must recreate
# the roles before it restores, keep the dump's privileges, and prove the api role can read.
restore_shape() {
  local file="$1" roles_line restore_line
  grep -q -- '--no-privileges' "$repo/deploy/backup/run.sh" && { echo "the dump drops privileges"; return 1; }
  grep -q 'roles.sql' "$file" || { echo "the drill does not recreate the roles"; return 1; }
  roles_line="$(grep -n 'ensure_roles ||' "$file" | head -1 | cut -d: -f1)"
  restore_line="$(grep -n 'pg_restore' "$file" | head -1 | cut -d: -f1)"
  [ -n "$roles_line" ] && [ -n "$restore_line" ] && [ "$roles_line" -lt "$restore_line" ] \
    || { echo "the roles are not recreated before the restore"; return 1; }
  grep -q -- '--no-privileges' "$file" && { echo "the restore drops privileges"; return 1; }
  grep -q 'SET ROLE cmslearn_api_user' "$file" || { echo "the drill does not prove the api role can read"; return 1; }
  return 0
}
restore_shape "$deploy/backup/restore-drill.sh" \
  || fail "the restore drill does not bring the platform back: $(restore_shape "$deploy/backup/restore-drill.sh")"
# The case: the same check must reject the shape t3 met, a restore that leaves the platform unable to serve.
sed 's/SET ROLE cmslearn_api_user/SET ROLE nobody_reads_this/' "$deploy/backup/restore-drill.sh" >"$work/drill-broken.sh"
if restore_shape "$work/drill-broken.sh"; then
  fail "the gate accepted a restore that leaves the platform unable to serve"
fi
note "the drill recreates the roles before the restore, restores the privileges, and proves the api role reads"

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
  # The plan stamps the deployment owner, so the cluster must hold that role, exactly as the deployment's
  # database does; applying as postgres here would hide a plan the deployment cannot use.
  owner="$(sed -n 's/^POSTGRES_USER=//p' "$deploy/.env.example" | head -1)"
  psql -X -h "$cluster" -p "$port" -U postgres -q -c "CREATE ROLE \"$owner\" LOGIN SUPERUSER PASSWORD 'gate-owner'" >/dev/null 2>&1     || fail "could not create the deployment owner $owner"
  psql -X -h "$cluster" -p "$port" -U postgres -d cmslearn_roles -q -v ON_ERROR_STOP=1     -v api_password=gate_api -v generator_password=gate_generator -v certifier_password=gate_certifier     -v judge_password=gate_judge -v bank_password=gate_bank -v backup_password=gate_backup     -f "$deploy/roles.sql" >/dev/null 2>&1 || fail "deploy/roles.sql does not apply"
  PGHOST="$cluster" PGPORT="$port" PGDATABASE=cmslearn_roles PGUSER="$owner"     bash "$repo/tools/apply-plan.sh" >/dev/null 2>&1 || fail "the plan does not apply to the roles database"

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
