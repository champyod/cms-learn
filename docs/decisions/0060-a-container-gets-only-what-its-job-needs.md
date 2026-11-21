# 0060 — A container gets only what its job needs

**Status:** accepted, 2026-11-21, by the operator.

## 1. The problem

The compose file described the network and left the container to Docker's defaults. Measured on the
committed stack before this decision:

| Property | Services that had it |
|---|---|
| read-only root filesystem | 0 of 11 |
| every capability dropped | 0 of 11 |
| a process limit | 0 of 11 |
| a memory and CPU limit | 2 of 11 (judge, objects) |
| `no-new-privileges` | 10 of 11 (the judge's `security_opt` list replaced the anchor's) |

A container that can write its own root filesystem can replace the binary it is about to execute. A
container with Docker's default capability set — fourteen capabilities, including `CHOWN`, `SETUID`,
`MKNOD`, `NET_RAW` and `SETFCAP` — holds authority its job never uses, so a flaw in the service is
worth more than the service. And a process with no `pids` limit is one fork bomb away from taking the
host from every other service; the memory and CPU limits existed for the two services somebody had
thought about, not because those two were the only ones needing a bound.

The contract in `tools/check-deploy.sh` asserted one of these facts (no-new-privileges, judge
exempted) and none of the others, so the compose file was a review artifact rather than a gate.

## 2. The decision

One rule, applied per service: **a container gets only what its own job needs, and the gate asserts
the exact set.** The anchor carries the facts that are true for every service — `read_only: true`,
`cap_drop: [ALL]`, `no-new-privileges` — and each service names the exceptions beside itself.

### 2.1 Read-only root filesystem, writable paths named

Every service inherits `read_only: true`. A directory it writes is either a named volume or a
bounded `tmpfs`, and every bind mount is read-only except the judge's cgroup tree, which `isolate`
needs and ADR 0001 accepted.

| Service | Writable paths | Why those |
|---|---|---|
| proxy | `/data`, `/config`, `/var/log/caddy`; tmpfs `/tmp` | certificates, Caddy's autosaved config, the JSON access log CrowdSec tails |
| api | tmpfs `/tmp` | the admin socket at `CMSLEARN_ADMIN_SOCKET`; the image is documented as writing nothing else |
| judge | `judge-work` volume, `/sys/fs/cgroup` (rw); tmpfs `/tmp`, `/var/lib/isolate`, `/run/isolate` | the box tree the compiled program is executed from, isolate's box root and lock directory |
| database | `pgdata`; tmpfs `/tmp`, `/var/run/postgresql` | the data directory and the Unix socket directory |
| crowdsec | `crowdsec-db`, `crowdsec-etc`; tmpfs `/tmp` | the LAPI database and the configuration it rewrites on first start |
| prometheus | `prom-data`; tmpfs `/tmp` | the 30-day TSDB |
| grafana | `grafana-data`; tmpfs `/tmp` | its SQLite database and plugins |
| node-exporter | none | it reads `/proc` and `/sys` through the read-only host mount and writes nothing |
| backup | `backups`; tmpfs `/tmp` | the encrypted dumps |
| schema | tmpfs `/tmp` | it reads the plan through two read-only bind mounts and exits |
| objects | `objectdata`; tmpfs `/tmp` | every byte of the store |

`tmpfs` sizes are bounded (`/tmp` between 32m and 256m, isolate's box root at 512m) so a service that
fills its scratch space is killed by its own memory limit rather than the host's. Every tmpfs except isolate's
box root is mounted `noexec,nosuid,nodev`; the box root must allow execution because the compiled
submission is executed from it.

### 2.2 Capabilities: drop all, add back the named few

`cap_drop: [ALL]` is in the anchor, so a service that needs nothing gets nothing. The set below is
what each job actually uses; every capability in it was removed in turn and the process stopped at a
named step (section 3).

| Service | Capabilities added back | Why |
|---|---|---|
| api | none | unprivileged uid 10001 on ports 8080/8081; no setuid binary in the image |
| backup | none | runs as `postgres`, writes one volume |
| crowdsec | none | the LAPI and the inline AppSec rules bind high ports and write volumes |
| database | `CHOWN`, `DAC_OVERRIDE`, `SETUID`, `SETGID` | the entrypoint starts as root to make the data directory belong to its own user, then switches to it; `FOWNER` was in the first candidate set and the same start stayed healthy without it |
| grafana | none | runs as uid 472, writes one volume |
| judge | `SYS_ADMIN`, `SYS_CHROOT`, `SETUID`, `SETGID`, `CHOWN`, `DAC_OVERRIDE`, `FOWNER`, `NET_ADMIN` | `isolate` mounts the sandbox's box tree and `/proc` (`SYS_ADMIN`), enters the box (`SYS_CHROOT`), switches to the box uid (`SETUID`/`SETGID`), gives that uid the box tree (`CHOWN`), reads the submitted executable (`DAC_OVERRIDE`), chmods the box's own `/tmp` (`FOWNER`), and raises `lo` (`NET_ADMIN`). `MKNOD` was offered and is not needed: the same step is reached without it |
| node-exporter | none | one process reading the host's `/proc` through a read-only mount |
| objects | `SETUID`, `SETGID` | its entrypoint drops from root to the image's own user with `su-exec` |
| prometheus | none | runs as `nobody`, writes one volume |
| proxy | none | unprivileged uid 10001; the host maps 80/443 to 8080/8443 precisely so no low port is bound, and the image removes the base image's `cap_net_bind_service` file capability, which a dropped bounding set would turn into an exec failure |
| schema | none | runs as uid 10001, reads mounts, talks to one database, exits |

The judge's `security_opt` list replaces the anchor's list rather than merging with it, so it repeats
`no-new-privileges:true` beside `seccomp:unconfined`. The seccomp exception remains the one place the
stack loosens a Docker default, and it is still the documented reason ADR 0001 chose `isolate`.

### 2.3 Memory, CPU and process limits

The memory and CPU numbers are the ones `docs/infra/single-server.md` already recommends for these
services. Until now the compose carried limits for two of them, so the recommendation was prose that
nothing enforced; this decision makes it the runtime bound and adds the process limit the document did
not have. The judge's per-submission limits stay where they belong — `isolate --mem` and
`--processes` — so its container limit covers the compiler and the supervisor, not the submission.

| Service | Memory | CPUs | Processes | Sized from |
|---|---|---|---|---|
| proxy | 256m | 0.3 | 256 | TLS, HTTP/2 and HTTP/3, rate-limit zones, a CrowdSec call per request, the access log |
| api | 512m | 1.0 | 512 | page rendering and long-lived streams; a thread pool and per-connection buffers |
| judge | 2g | 2.0 | 512 | the compiler is the largest thing the stack runs; the submission itself is bounded by `isolate` |
| database | 2g | 1.0 | 256 | `shared_buffers`, one backend per connection, autovacuum workers |
| crowdsec | 256m | 0.3 | 128 | the Go LAPI plus the CRS rule set evaluated inline |
| prometheus | 512m | 0.5 | 128 | a 30-day TSDB head and scrape buffers |
| grafana | 256m | 0.3 | 256 | the Go server plus one plugin subprocess per datasource |
| node-exporter | 64m | 0.1 | 64 | one process reading `/proc` and `/sys` |
| backup | 128m | 0.2 | 64 | `pg_dump` streams to `age`, so memory follows the row buffer, not the database size |
| schema | 256m | 0.5 | 64 | one `pgschema apply`, then it exits |
| objects | 512m | 0.5 | 256 | master, volume server, filer and S3 gateway in one Go process; the bytes are on disk |

The process limits are new here: they cover the threads and child processes each service actually
creates, and the judge's submission process count remains isolate's `--processes`, not this bound.
`schema` is the one row the infrastructure document does not have; a one-shot apply gets half a core
and 256 MB.

## 3. The proof

`tools/check-deploy.sh` reads the rendered compose, then holds it against two tables that are the
contract: `CAP_ADD` (the exact set above) and `WRITABLE_PATHS` (the exact immutable set of volume and
tmpfs targets). A service missing from either table fails as loudly as one whose row changed, so a
new service cannot join the stack without somebody deciding why it needs what it asks for. The same
block asserts `read_only`, `cap_drop: [ALL]`, `no-new-privileges`, the seccomp exception being the
judge's alone, and that memory, CPU and process limits are present and positive.

The gate is proved to bite: `tools/falsify/cases/deploy.ts` gained eleven cases — read-only rootfs
removed, capabilities not dropped, an unneeded capability added to the api, a needed one removed from
the judge, `no-new-privileges` removed from the judge alone, each of the three limit kinds removed, a
judge writable path unmounted, a bind mount made writable, and the proxy's file capability put back. All
22 cases behave.

Every row above was started on this machine as the container the compose defines, with the image the
release pins (or the Dockerfile it builds), the capabilities listed, `read_only: true`, the tmpfs
entries and the volume mounts:

| Image | Result |
|---|---|
| proxy (built from `deploy/proxy`) | healthy; zero capabilities; the binary carries no file capability |
| postgres 18.6 (pinned digest) | healthy; `CHOWN`, `DAC_OVERRIDE`, `SETUID`, `SETGID` |
| seaweedfs 4.51 (pinned digest) | healthy; `SETUID`, `SETGID`; without them `su-exec: setgroups: Operation not permitted` |
| crowdsec v1.8.2 (pinned digest) | healthy; zero capabilities |
| prometheus v3.15.1 (pinned digest) | ready; zero capabilities |
| grafana 13.3.0 (pinned digest) | serving `/api/health` after ~44s at 0.3 CPU; zero capabilities |
| node-exporter v1.9.1 (pinned digest) | running; zero capabilities, read-only host mount |
| backup (built from `deploy/backup`) | healthy; zero capabilities |

One assertion lives in the gate rather than in the compose: the proxy binary carries no file
capability. A binary carrying one outside the container's bounding set cannot be executed at all, and
that is how this was found — the first proxy container with `cap_drop: [ALL]` exited with
`exec /usr/bin/caddy: operation not permitted`. The image drops the base image's
`cap_net_bind_service` at build time, and the gate reads it back with `getcap`.

The capabilities were found, not guessed. Removing one at a time from the set above produced:

```
minus SYS_ADMIN      Cannot run proxy, clone failed: Operation not permitted
minus SYS_CHROOT     Chroot failed: Operation not permitted
minus SETUID         setresuid: Operation not permitted
minus SETGID         Proxy failed before it passed box_pid: Interrupted system call
minus CHOWN          Cannot chown busybox: Operation not permitted
minus DAC_OVERRIDE   Cannot change current directory: Permission denied
minus FOWNER         Cannot chmod ./tmp/tmp: Operation not permitted
minus NET_ADMIN      SIOCSIFFLAGS on 'lo' failed: Operation not permitted
```

Those runs used the sandbox image the isolation gate builds (`judge/isolation/Dockerfile`), because
the judge image itself is not in this repository — `release.toml` declares it `unpublished`. So the
judge row is the one row proved against the component it delegates to rather than against the final
container; proving the judge container end to end belongs to the deployment run.

## 4. What this rejects

- **Privileged containers.** They would make the limits and the capability drop decoration.
- **Docker's default capability set minus a few.** The default is fourteen capabilities; eight of the
  eleven services need none of them, and "minus a few" leaves the next service with authority nobody
  argued for.
- **A writable root filesystem with the application tree mounted read-only.** The process can still
  replace its own binary or drop a shared library beside it; the writable thing that matters is the
  filesystem the executable lives on.
- **Sizing every service the same.** A proxy and a compiler do not need the same memory; a limit that
  is generous for one service is a recipe for the other to be killed.
- **A CSP in this decision.** `deploy/Caddyfile` already carries a security-header block, and that is
  where a policy will go, but the policy's content is a separate decision; this one does not choose
  it.

## 5. What was found while proving, and left for its owner

- **`schema` cannot resolve its tool.** It mounts `deploy/plan` and `tools` at `/app/...` and runs
  `tools/apply-plan.sh`, which sources `tools/pinned-pgschema.sh`; that script reads
  `docs/dependencies.md` beside `tools`. The file is not mounted, so the version pin cannot be read
  and the migrate step refuses before connecting. That is a deployment-path defect, not a hardening
  one, and fixing it changes what the migrate container mounts; it is recorded here so the next
  deploy run does not spend its time there.
- **The judge image does not exist yet** (`release.toml` kind `unpublished`), so the judge's capability
  set is derived from the sandbox image and asserted in compose, not proven in the final container.
- **The sandbox image on this host did not execute a program.** `isolate --init` succeeded and every
  setup step passed with the set above, but the final `execve` returned `No such file or directory`
  (`status: RE`) even with a statically linked binary. The isolation gate accepts `RE` as contained,
  so the same result would read as a pass. Whether that is this host's kernel (7.3.4) or the image is
  not established here; it is recorded because the deployment proof depends on it.
