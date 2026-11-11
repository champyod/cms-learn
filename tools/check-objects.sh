#!/usr/bin/env bash
# Object-store gate.
#
# Task bytes live in object storage and the row holds only the key (ADR 0023). Two things can go wrong
# and both are silent: a missing object read as empty content, and a deployment that has no credential.
# This gate proves the client refuses the first by name and that the deployment refuses to start without
# the second, against a local store that speaks S3, so the check needs no network and no account.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
deploy="$repo/deploy"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

fail() { printf 'objects gate FAILED: %s\n' "$*" >&2; exit 1; }
note() { printf '  %s\n' "$*"; }

reader="${CMS_OBJECT_GET:-$repo/build/safety/core/cms_object_get}"
[ -x "$reader" ] || fail "no reader at $reader; build it with: cmake --build --preset safety"

# ── 1. one client ────────────────────────────────────────────────────────────────────────────────
# Anything in the tree that names the SDK's own types is a second client, and a second client is a
# second place the key shape and the refusal rules can drift.
others="$(grep -rln "aws/s3/\|Aws::S3" --include=*.cpp --include=*.hpp "$repo" 2>/dev/null |
  grep -v "^$repo/build/" | grep -v "^$repo/core/src/object_store.cpp$" || true)"
[ -z "$others" ] || fail "a second client speaks to the store: $others"
note "one client: core/src/object_store.cpp"

# ── 2. the deployment names the keys, empty, and the compose refuses without them ────────────────
for key in CMSLEARN_S3_ENDPOINT CMSLEARN_S3_REGION CMSLEARN_S3_BUCKET CMSLEARN_S3_ACCESS_KEY_ID \
  CMSLEARN_S3_SECRET_ACCESS_KEY; do
  grep -q "^$key=" "$deploy/host.env.example" || fail "deploy/host.env.example does not name $key"
  value="$(sed -n "s/^$key=//p" "$deploy/host.env.example")"
  case "$key" in
    *ACCESS_KEY_ID | *SECRET_ACCESS_KEY) [ -z "$value" ] || fail "deploy/host.env.example ships a credential in $key" ;;
    *) [ -n "$value" ] || fail "deploy/host.env.example leaves $key empty" ;;
  esac
done
for key in CMSLEARN_S3_ACCESS_KEY_ID CMSLEARN_S3_SECRET_ACCESS_KEY; do
  grep -q "^$key=$" "$deploy/.env.example" || fail "deploy/.env.example does not name $key, empty"
  grep -q "\${$key:?" "$deploy/docker-compose.yml" || fail "docker-compose.yml does not require $key"
done
note "the examples name the keys empty; the compose requires the credential"

# ── 3. no credential in a tracked file ───────────────────────────────────────────────────────────
# A literal that looks like a store credential is a leak whether or not it is the live one.
if git -C "$repo" ls-files -z | xargs -0 grep -lnE "AKIA[0-9A-Z]{16}|aws_secret_access_key *= *[A-Za-z0-9/+=]{20,}" 2>/dev/null |
  grep -q .; then
  fail "a tracked file carries what looks like a store credential"
fi
note "no credential in a tracked file"

# ── 4. the client's behaviour, against a local S3 ────────────────────────────────────────────────
port=$((34800 + (RANDOM % 200)))
python3 - "$port" "$work" <<'PY' &
import http.server, socketserver, sys
port, work = int(sys.argv[1]), sys.argv[2]

class Store(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path.endswith("/tasks/alpha/statement.th.md"):
            body = "ผลรวมของช่วงข้อมูล".encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/markdown")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        body = b"<?xml version='1.0'?><Error><Code>NoSuchKey</Code><Message>missing</Message></Error>"
        self.send_response(404)
        self.send_header("Content-Type", "application/xml")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass

with socketserver.TCPServer(("127.0.0.1", port), Store) as server:
    open(f"{work}/ready", "w").close()
    server.serve_forever()
PY
server=$!
for _ in $(seq 1 50); do [ -f "$work/ready" ] && break; sleep 0.1; done
[ -f "$work/ready" ] || fail "the local store did not start"

export CMSLEARN_S3_ENDPOINT="http://127.0.0.1:$port"
export CMSLEARN_S3_REGION="us-east-1"
export CMSLEARN_S3_BUCKET="cms-learn"
export CMSLEARN_S3_ACCESS_KEY_ID="probe-access-key"
export CMSLEARN_S3_SECRET_ACCESS_KEY="probe-secret-key"

set +e
present="$("$reader" tasks/alpha/statement.th.md 2>"$work/present.err")"
present_status=$?
missing="$("$reader" tasks/alpha/gone.md 2>"$work/missing.err" >/dev/null)"
missing_status=$?
unset CMSLEARN_S3_BUCKET
unconfigured="$("$reader" tasks/alpha/statement.th.md 2>"$work/config.err" >/dev/null)"
config_status=$?
set -e

[ "$present_status" -eq 0 ] || fail "a present object was refused (exit $present_status): $(cat "$work/present.err")"
[ "$present" = "ผลรวมของช่วงข้อมูล" ] || fail "the bytes came back changed: '$present'"
[ "$missing_status" -eq 4 ] || fail "a missing object did not refuse with not-found (exit $missing_status)"
grep -q "tasks/alpha/gone.md" "$work/missing.err" || fail "the not-found message does not name the key"
grep -qi "empty\|no bytes" "$work/missing.err" && fail "a missing object was described as empty content"
[ "$config_status" -eq 2 ] || fail "an unconfigured store did not refuse (exit $config_status)"
grep -q "bucket" "$work/config.err" || fail "the refusal does not name the missing key"
grep -q "probe-secret-key" "$work/config.err" && fail "the refusal printed the credential"

note "a present object returns its bytes"
note "a missing object is not-found naming the key, never empty content"
note "an unconfigured store refuses, naming what is missing and not the secret"

kill "$server" 2>/dev/null || true
wait "$server" 2>/dev/null || true
printf 'objects gate: OK\n'
