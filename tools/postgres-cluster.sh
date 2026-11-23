#!/usr/bin/env bash
# The one place a gate starts its scratch PostgreSQL cluster.
#
# Every gate that needs a server sources this file instead of repeating initdb and pg_ctl: the cluster is
# the same in every gate, and it has to behave the same on a developer's shell and in a CI container.
# PostgreSQL refuses to run as root, and a CI container's steps run as root, so when the caller is root the
# server commands are handed to the unprivileged postgres user while the gate keeps its own identity.
#
# Functions:
#   pg_cluster_binaries                 put initdb/pg_ctl on PATH when the distribution keeps them off it
#   pg_cluster_owner                    print the OS user the server runs as
#   pg_cluster_prepare <dir>            make <dir> usable by that user; refuses when root has no such user
#   pg_cluster_init <dir>               create the data directory in <dir>
#   pg_cluster_start <dir> <port> <log> start the server and wait until it accepts connections
#   pg_cluster_stop <dir>               stop the server
#
# The caller keeps its own error messages: prepare, init and start return non-zero and print the reason
# on stderr, and the gate adds the log path a reader needs.

# Debian and Ubuntu install the server binaries under /usr/lib/postgresql/<version>/bin, which is not on
# PATH. This is best-effort: the gate's own tool check remains the authority on what is installed.
pg_cluster_binaries() {
  if command -v initdb >/dev/null 2>&1 && command -v pg_ctl >/dev/null 2>&1; then
    return 0
  fi
  local dir
  dir="$(printf '%s\n' /usr/lib/postgresql/*/bin | sort | tail -1)"
  if [ -x "$dir/initdb" ]; then
    PATH="$dir:$PATH"
    export PATH
  fi
}

pg_cluster_owner() {
  if [ "$(id -u)" -eq 0 ]; then
    printf 'postgres'
  else
    id -un
  fi
}

pg_cluster_prepare() {
  local work="$1" owner
  owner="$(pg_cluster_owner)"
  if [ "$owner" = "$(id -un)" ]; then
    return 0
  fi
  if ! id -u "$owner" >/dev/null 2>&1; then
    printf 'postgres-cluster: the caller is root and there is no %s user to own the server\n' "$owner" >&2
    return 1
  fi
  if ! command -v runuser >/dev/null 2>&1; then
    printf 'postgres-cluster: the caller is root and runuser is not installed\n' >&2
    return 1
  fi
  chown "$owner" "$work"
}

pg_cluster_run() {
  local owner
  owner="$(pg_cluster_owner)"
  if [ "$owner" = "$(id -un)" ]; then
    "$@"
  else
    runuser -u "$owner" -- "$@"
  fi
}

pg_cluster_init() {
  local work="$1"
  pg_cluster_run initdb -D "$work/data" -U postgres -A trust
}

pg_cluster_start() {
  local work="$1" port="$2" log="$3"
  pg_cluster_run pg_ctl -D "$work/data" \
    -o "-c listen_addresses='' -c unix_socket_directories=$work -p $port" \
    -l "$log" -w start
}

pg_cluster_stop() {
  local work="$1"
  pg_cluster_run pg_ctl -D "$work/data" -m immediate stop
}

# The owner the deployment's plan is rendered for, read from the deployment's own source: a role change
# cannot leave the harness behind, and no second list can drift from it.
pg_cluster_deployment_owner() {
  local repo="$1"
  sed -n 's/^POSTGRES_USER=//p' "$repo/deploy/.env.example" | tail -1
}

pg_cluster_require_owner() {
  local work="$1" port="$2" owner="$3" present
  present="$(psql -X -h "$work" -p "$port" -U postgres -tAc \
    "SELECT 1 FROM pg_roles WHERE rolname = '${owner}'" 2>/dev/null)"
  if [ "$present" != "1" ]; then
    printf 'postgres-cluster: the deployment owner role %s is missing from the scratch cluster; apply-plan would fail on it\n' \
      "$owner" >&2
    return 1
  fi
}

pg_cluster_create_owner() {
  local repo="$1" work="$2" port="$3" owner
  owner="$(pg_cluster_deployment_owner "$repo")"
  if [ -z "$owner" ]; then
    printf 'postgres-cluster: deploy/.env.example names no POSTGRES_USER, so the cluster cannot mirror a deployment\n' >&2
    return 1
  fi
  if [ "$(psql -X -h "$work" -p "$port" -U postgres -tAc "SELECT 1 FROM pg_roles WHERE rolname = '${owner}'" 2>/dev/null)" != "1" ]; then
    psql -X -h "$work" -p "$port" -U postgres -q -v ON_ERROR_STOP=1 \
      -c "CREATE ROLE \"${owner}\" LOGIN SUPERUSER" >/dev/null 2>&1 || true
  fi
  pg_cluster_require_owner "$work" "$port" "$owner"
}
