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

# 6: privilege escalation
for name, svc in services.items():
    opts = svc.get("security_opt") or []
    escalates = not any(str(o).startswith("no-new-privileges") for o in opts)
    if escalates and name != "judge":
        errors.append(f"{name} does not set no-new-privileges:true")
if "seccomp:unconfined" in json.dumps(services.get("judge", {})):
    pass  # documented, required by isolate
else:
    errors.append("judge does not document its seccomp exception")

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
print(f"  {len(services)} services, exposure and isolation invariants hold")
PY

note "caddy validates the Caddyfile with the modules it will run"
proxy_image="cms-learn-proxy:gate"
docker build -q -t "$proxy_image" "$deploy/proxy" >/dev/null || fail "proxy image did not build"
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

printf 'deploy gate: OK\n'
