#!/usr/bin/env bash
# Object-store gate.
#
# Task bytes live in object storage and the row holds only the key (ADR 0023). Three things can go wrong and
# all are silent: a missing object read as empty content, a deployment that has no credential, and a stored
# object whose bytes no longer match the digest its row records. This gate proves the client refuses the first
# by name, that the deployment refuses to start without the second, and that the third is a refusal rather than
# different content — against a local store that speaks S3, so the check needs no network and no account.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
deploy="$repo/deploy"
work="$(mktemp -d)"
store_pid=""
cleanup() {
  [ -n "$store_pid" ] && kill "$store_pid" 2>/dev/null
  rm -rf "$work"
}
trap cleanup EXIT

fail() { printf 'objects gate FAILED: %s\n' "$*" >&2; exit 1; }
note() { printf '  %s\n' "$*"; }

reader="${CMS_OBJECT_GET:-$repo/build/safety/core/cms_object_get}"
writer="${CMS_OBJECT_PUT:-$repo/build/safety/core/cms_object_put}"
[ -x "$reader" ] || fail "no reader at $reader; build it with: cmake --build --preset safety"
[ -x "$writer" ] || fail "no writer at $writer; build it with: cmake --build --preset safety"

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

# ── 4. the proof both readers rest on actually ran ──────────────────────────────────────────────
# The gate above proves the contract of the client; the case below proves the two readers against a real S3
# gateway. A suite that drops it cannot report success: a skip here is a failure of this gate, with the reason
# the case gave, so "no docker" reads as "the proof did not run" and never as a pass.
readers="$repo/api/tests/object_readers_e2e.sh"
api_binary="${CMS_API_BINARY:-$repo/build/safety/api/cms_api}"
[ -f "$readers" ] || fail "no readers case at $readers"
[ -x "$api_binary" ] || fail "no api binary at $api_binary; build it with: cmake --build --preset safety"
set +e
bash "$readers" "$api_binary" >"$work/readers.log" 2>&1
readers_status=$?
set -e
if [ "$readers_status" -eq 77 ]; then
  printf 'objects gate FAILED: the object readers proof did not run, it skipped — %s\n' \
    "$(grep -m1 'skipped' "$work/readers.log" || tail -1 "$work/readers.log")" >&2
  exit 1
fi
[ "$readers_status" -eq 0 ] || fail "the object readers case failed: $(tail -3 "$work/readers.log")"
note "the object readers case ran and passed ($(grep -c '\[ok\]' "$work/readers.log") assertion(s))"

# ── 5. a local store the client can write to and read back from ──────────────────────────────────
port=$((34800 + (RANDOM % 200)))
# The SDK sends a PUT as `aws-chunked` with `Transfer-Encoding: chunked` (no Content-Length), so the body is
# decoded from its chunks; a handler that read Content-Length only would store empty bytes and the read would
# pass for the wrong reason.
python3 - "$port" "$work" <<'PY' >"$work/store.log" 2>&1 &
import http.server, re, socketserver, sys
from urllib.parse import urlsplit

port, work = int(sys.argv[1]), sys.argv[2]
objects = {"tasks/alpha/statement.th.md": "ผลรวมของช่วงข้อมูล".encode()}

class Store(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def key(self):
        return urlsplit(self.path).path.lstrip("/").partition("/")[2]

    def do_GET(self):
        body = objects.get(self.key())
        if body is None:
            body = b"<?xml version='1.0'?><Error><Code>NoSuchKey</Code><Message>missing</Message></Error>"
            self.reply(404, body, "application/xml")
            return
        self.reply(200, body)

    def read_body(self):
        if self.headers.get("Transfer-Encoding", "").lower() != "chunked":
            return self.rfile.read(int(self.headers.get("Content-Length", "0")))
        body = b""
        while True:
            size = int(self.rfile.readline().split(b";")[0].strip() or b"0", 16)
            if size == 0:
                while self.rfile.readline() not in (b"\r\n", b"\n", b""):
                    pass
                break
            body += self.rfile.read(size)
            self.rfile.read(2)
        while self.looks_chunked(body):
            body = self.dechunk(body)
        return body

    @staticmethod
    def looks_chunked(data):
        return re.match(rb"^[0-9A-Fa-f]{1,8}\r\n", data) is not None and data.endswith(b"\r\n\r\n")

    @staticmethod
    def dechunk(data):
        body, position = b"", 0
        while True:
            end = data.find(b"\r\n", position)
            if end < 0:
                return body
            size = int(data[position:end].split(b";")[0].strip() or b"0", 16)
            position = end + 2
            if size == 0:
                return body
            body += data[position:position + size]
            position += size + 2

    def do_PUT(self):
        objects[self.key()] = self.read_body()
        self.reply(200, b"")

    def reply(self, status, body, kind="text/markdown"):
        self.send_response(status)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("ETag", '"cms-learn-gate"')
        self.end_headers()
        if body:
            self.wfile.write(body)

    def log_message(self, *args):
        pass

with socketserver.TCPServer(("127.0.0.1", port), Store) as server:
    open(f"{work}/ready", "w").close()
    server.serve_forever()
PY
store_pid=$!
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
set -e

[ "$present_status" -eq 0 ] || fail "a present object was refused (exit $present_status): $(cat "$work/present.err")"
[ "$present" = "ผลรวมของช่วงข้อมูล" ] || fail "the bytes came back changed: '$present'"
[ "$missing_status" -eq 4 ] || fail "a missing object did not refuse with not-found (exit $missing_status)"
grep -q "tasks/alpha/gone.md" "$work/missing.err" || fail "the not-found message does not name the key"
grep -qi "empty\|no bytes" "$work/missing.err" && fail "a missing object was described as empty content"
note "a present object returns its bytes"
note "a missing object is not-found naming the key, never empty content"

# ── 6. the write path, and the digest that makes the read content-addressed ─────────────────────
printf 'the invariant holds for every n' >"$work/proof.md"
set +e
digest="$("$writer" tasks/alpha/proof.md "$work/proof.md" 2>"$work/put.err")"
put_status=$?
set -e
[ "$put_status" -eq 0 ] || fail "the writer refused the object (exit $put_status): $(cat "$work/put.err")"
expected="$(sha256sum "$work/proof.md" | cut -d' ' -f1)"
[ "$digest" = "$expected" ] || fail "the writer named $digest, the bytes hash to $expected"
note "a write returns the digest of the bytes it stored"

set +e
returned="$("$reader" tasks/alpha/proof.md "$digest" 2>"$work/verified.err")"
verified_status=$?
set -e
[ "$verified_status" -eq 0 ] || fail "the verifying read refused its own bytes: $(cat "$work/verified.err")"
[ "$returned" = "$(cat "$work/proof.md")" ] || fail "the bytes came back changed: '$returned'"
note "the bytes read back through their digest are the bytes written"

wrong="$(printf '%064d' 0)"
set +e
"$reader" tasks/alpha/proof.md "$wrong" >"$work/wrong.out" 2>"$work/wrong.err"
wrong_status=$?
set -e
[ "$wrong_status" -eq 1 ] || fail "a wrong digest did not refuse (exit $wrong_status)"
[ -s "$work/wrong.out" ] && fail "a wrong digest still returned bytes: $(cat "$work/wrong.out")"
grep -q "does not match the digest" "$work/wrong.err" || fail "the refusal does not say the digest did not match"
grep -q "$digest" "$work/wrong.err" || fail "the refusal does not name the digest the row records"
grep -q "$wrong" "$work/wrong.err" || fail "the refusal does not name the digest the bytes hash to"
note "bytes that do not match their digest are refused, naming both addresses"

set +e
"$reader" tasks/alpha/proof.md "not-a-digest" >"$work/shape.out" 2>"$work/shape.err"
shape_status=$?
set -e
[ "$shape_status" -eq 1 ] || fail "a row without an address did not refuse (exit $shape_status)"
grep -q "no content address" "$work/shape.err" || fail "the refusal does not name the missing address"
note "a row that records no address is refused, not read unverified"

# The same key published with different bytes is the fault content addressing exists for: the row still names
# the first digest, so the read must refuse rather than serve whatever the store now holds.
printf 'the invariant holds for every n, as stated' >"$work/proof-v2.md"
"$writer" tasks/alpha/proof.md "$work/proof-v2.md" >/dev/null || fail "the second write was refused"
set +e
"$reader" tasks/alpha/proof.md "$digest" >"$work/replaced.out" 2>"$work/replaced.err"
replaced_status=$?
set -e
[ "$replaced_status" -eq 1 ] || fail "an object replaced under its row did not refuse (exit $replaced_status)"
[ -s "$work/replaced.out" ] && fail "an object replaced under its row was returned anyway"
grep -q "does not match the digest" "$work/replaced.err" ||
  fail "the replaced-object refusal does not say the digest did not match"
note "an object replaced under its row is refused, not served as the new version"

# An unconfigured store is the deployment mistake, and the refusal must name what is missing.
set +e
unset CMSLEARN_S3_BUCKET
unconfigured="$("$reader" tasks/alpha/statement.th.md 2>"$work/config.err" >/dev/null)"
config_status=$?
set -e
[ "$config_status" -eq 2 ] || fail "an unconfigured store did not refuse (exit $config_status)"
grep -q "bucket" "$work/config.err" || fail "the refusal does not name the missing key"
grep -q "probe-secret-key" "$work/config.err" && fail "the refusal printed the credential"
note "an unconfigured store refuses, naming what is missing and not the secret"

printf 'objects gate: OK\n'
