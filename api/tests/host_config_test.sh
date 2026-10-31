#!/usr/bin/env bash
# Proves the api refuses to start on an incomplete host file, and says why.
#
# The service deliberately has no defaults: a wrong address or a short seal key would start cleanly and
# fail later, in a way nobody connects to the file they edited. Each case below removes or corrupts one
# fact and requires the binary to stop with a message naming it.
set -uo pipefail

binary="${1:?usage: host_config_test.sh <path to cms_api>}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

pass=0
fail=0

behaved() { printf '  [ok]   %s\n' "$1"; pass=$((pass + 1)); }
broken() { printf '  [!!]   %s — %s\n' "$1" "$2"; fail=$((fail + 1)); }

write_host_file() {
  local path="$1"
  shift
  printf '%s\n' "$@" >"$path"
}

complete_file() {
  write_host_file "$1" \
    'CMSLEARN_LISTEN_ADDRESS=127.0.0.1' \
    'CMSLEARN_LISTEN_PORT=8080' \
    'CMSLEARN_DATABASE_URL=postgresql://cmslearn@database/cmslearn' \
    'CMSLEARN_SEAL_KEY=0123456789abcdef0123456789abcdef' \
    'CMSLEARN_BOOTSTRAP_ADMIN=operator@example.org' \
    'CMSLEARN_TLS_MODE=proxy'
}

expect_failure_naming() {
  local name="$1" key="$2"
  shift 2
  local out status
  out="$(env -u CMSLEARN_HOST_FILE "$@" "$binary" --check 2>&1)"
  status=$?
  if [ "$status" -eq 0 ]; then
    broken "$name" "the service accepted a host file it should have refused"
  elif [[ "$out" != *"$key"* ]]; then
    broken "$name" "the refusal did not name $key: $out"
  else
    behaved "$name"
  fi
}

case_file="$work/complete.env"
complete_file "$case_file"

out="$(env -u CMSLEARN_HOST_FILE "$binary" --check 2>&1)"
if [ $? -ne 0 ] && [[ "$out" == *"CMSLEARN_HOST_FILE is not set"* ]]; then
  behaved "no CMSLEARN_HOST_FILE is refused by name"
else
  broken "no CMSLEARN_HOST_FILE is refused by name" "got: $out"
fi

expect_failure_naming "an unreadable path is refused" "cannot read CMSLEARN_HOST_FILE" \
  CMSLEARN_HOST_FILE="$work/absent.env"

missing_file="$work/missing.env"
write_host_file "$missing_file" 'CMSLEARN_LISTEN_ADDRESS=127.0.0.1'
expect_failure_naming "a missing fact is named" "CMSLEARN_SEAL_KEY" \
  CMSLEARN_HOST_FILE="$missing_file"

short_key_file="$work/short-key.env"
write_host_file "$short_key_file" \
  'CMSLEARN_LISTEN_ADDRESS=127.0.0.1' \
  'CMSLEARN_LISTEN_PORT=8080' \
  'CMSLEARN_DATABASE_URL=postgresql://cmslearn@database/cmslearn' \
  'CMSLEARN_SEAL_KEY=too-short' \
  'CMSLEARN_BOOTSTRAP_ADMIN=operator@example.org' \
  'CMSLEARN_TLS_MODE=proxy'
expect_failure_naming "a short seal key is refused" "shorter than 32 bytes" \
  CMSLEARN_HOST_FILE="$short_key_file"

bad_url_file="$work/bad-url.env"
write_host_file "$bad_url_file" \
  'CMSLEARN_LISTEN_ADDRESS=127.0.0.1' \
  'CMSLEARN_LISTEN_PORT=8080' \
  'CMSLEARN_DATABASE_URL=mysql://nope' \
  'CMSLEARN_SEAL_KEY=0123456789abcdef0123456789abcdef' \
  'CMSLEARN_BOOTSTRAP_ADMIN=operator@example.org' \
  'CMSLEARN_TLS_MODE=proxy'
expect_failure_naming "a non-postgres URL is refused" "not a postgresql:// URL" \
  CMSLEARN_HOST_FILE="$bad_url_file"

bad_tls_file="$work/bad-tls.env"
write_host_file "$bad_tls_file" \
  'CMSLEARN_LISTEN_ADDRESS=127.0.0.1' \
  'CMSLEARN_LISTEN_PORT=8080' \
  'CMSLEARN_DATABASE_URL=postgresql://cmslearn@database/cmslearn' \
  'CMSLEARN_SEAL_KEY=0123456789abcdef0123456789abcdef' \
  'CMSLEARN_BOOTSTRAP_ADMIN=operator@example.org' \
  'CMSLEARN_TLS_MODE=maybe'
expect_failure_naming "an unknown tls mode is refused" "neither proxy nor direct" \
  CMSLEARN_HOST_FILE="$bad_tls_file"

duplicate_file="$work/duplicate.env"
complete_file "$duplicate_file"
printf 'CMSLEARN_LISTEN_PORT=9090\n' >>"$duplicate_file"
expect_failure_naming "a repeated key is refused" "repeats CMSLEARN_LISTEN_PORT" \
  CMSLEARN_HOST_FILE="$duplicate_file"

out="$(CMSLEARN_HOST_FILE="$case_file" "$binary" --check 2>&1)"
if [ $? -eq 0 ] && [[ "$out" == *"configuration is valid"* ]]; then
  behaved "a complete host file is accepted"
else
  broken "a complete host file is accepted" "got: $out"
fi

printf '\n%s assertion(s) behaved, %s did not\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
