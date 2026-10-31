# Single-server deployment — proposal

Goal: everything `cms-learn` needs on **one server**, deployable by one operator, with the security
surface written down rather than assumed. This document is the inventory; the decisions it leaves
open are asked as questions and then recorded in an ADR.

Constraints inherited from the hub, not negotiable here:

- **No host installs, scripts or host environment variables.** OS, kernel and engine patching stays
  operator-manual. Every service is a container; the only host interactions allowed are a read-only
  mount and the cgroup delegation `isolate` already needs.
- **The contest stack is untouched.** Nothing here shares a kernel-time path with `cms-docker`: no
  shared database, no shared network, no shared volumes. If both live on one machine it is as two
  independent compose projects.
- **Students are minors.** Every stored field is justified, every credential is hashed, and the
  approval gate fails closed.
- **30–60 students at launch**, desktop + mobile, TH/EN.

## Sizing

| Resource | Recommendation | Why |
|---|---|---|
| vCPU | 4 | API + web are idle-mostly; the judge is the sink. One submission ≈ one core for ~1 s |
| RAM | 8 GB | Postgres 1–2 GB, judge worker 2 GB, WAF/proxy/monitoring ~1.5 GB, headroom for build spikes |
| Disk | 80 GB NVMe | images ~6 GB, Postgres small, submissions + logs are the growth |
| Swap | none on the judge path | a swapping judge reports wrong time limits; better to queue |
| Kernel | ≥ 5.15 with cgroup v2 | `isolate` delegation, same shape as the contest worker |

If submissions ever exceed ~5/s sustained, the judge moves to its own box — that is a scale event,
not a launch requirement.

## Service inventory

Only the proxy is publicly bound. Everything else listens on the internal compose network or
`127.0.0.1`, and the judge has **no egress at all**.

| Service | Image (pinned) | Public | Volume | Limits | Role |
|---|---|---|---|---|---|
| `proxy` | `caddy:2.11.7` (uid 10001, ports 8080/8443 inside; host maps 80/443) | 80/443 | `caddy-data`, `caddy-config` | 0.3 cpu / 256 MB | TLS (ACME), HTTP→HTTPS, HSTS, rate limits, path routing, static caching |
| `web` | `ghcr.io/champyod/cms-learn-web:<sha>` | no | — | 0.5 cpu / 512 MB | Drogon + Inja + htmx (C++23) |
| `api` | `ghcr.io/champyod/cms-learn-api:<sha>` | no | — | 1 cpu / 512 MB | Drogon (C++23) + shared domain core: auth, sessions, progression, serving |
| `judge` | `ghcr.io/champyod/cms-learn-judge:<sha>` | no | `judge-work`, `judge-data` | 2 cpu / 2 GB, `cgroup: host`, cgroups rw | C++ core + `isolate`; no network |
| `database` | `postgres:18.6` | no | `pgdata` | 1 cpu / 2 GB | Postgres, not published |
| `objects` | `seaweedfs:4.51` (`weed server -s3`) | no | `objectdata` | 0.5 cpu / 512 MB | S3-compatible store: task statements, source, test archives |
| `waf` | `crowdsecurity/crowdsec:v1.8.2` + Caddy bouncer | no | `crowdsec-data` | 0.3 cpu / 256 MB | AppSec CRS rules + IP reputation, ban decisions |
| `metrics` | `prom/prometheus:v3.15.1` | no | `prom-data` | 0.5 cpu / 512 MB | Scrapes API/judge/node exporters |
| `dashboards` | `grafana/grafana:13.3.0` | no (behind proxy + auth) | `grafana-data` | 0.3 cpu / 256 MB | Operator dashboards |
| `node-exporter` | `prom/node-exporter:v1.9.1` | no | `/proc`,`/sys` ro | 0.1 cpu / 64 MB | Host metrics |
| `backup` | `postgres:18.6` (client) + `age` | no | `pgdata` ro | 0.2 cpu / 128 MB | Nightly dump, encrypted, offsite sync, restore drill |

Deliberately absent: no Portainer, no Vault (nothing here needs dynamic secrets at this size), no
Redis unless sessions move there, no Kafka/queue broker (`Postgres` + `FOR UPDATE SKIP LOCKED` is
the queue until measurements say otherwise).

## Security controls, and what each one actually stops

| Layer | Control | Stops | Residual |
|---|---|---|---|
| Transport | Caddy ACME TLS, HSTS, TLS 1.2+ only | passive interception | — |
| Edge | CrowdSec AppSec (CRS) + IP reputation | SQLi/XSS/RCE probes, known-bad IPs | novel payloads get one shot |
| Edge | Caddy rate limit per IP + per session on `/api/auth/*` | credential stuffing, enumeration | distributed slow attacks |
| App | argon2id password hash, per-user salt, 72-byte cap enforced both sides | offline cracking from a dump | — |
| App | Opaque session id in `HttpOnly; Secure; SameSite=Lax` cookie, server-side session row | token theft via JS, CSRF on state-changing POST | — |
| App | CSRF token on forms, `Origin` check on POST | cross-site submission | — |
| App | Domain allowlist (fail-closed) + approval gate | strangers and non-school accounts | — |
| App | Per-account submission rate + queue depth cap | a student DoSing the judge | — |
| Judge | `isolate` namespaces/cgroups/seccomp, no network namespace, rlimits, wall-clock kill | fork bombs, egress, filesystem access, CPU burn | kernel-level escape (shared kernel) |
| Data | Postgres not published, scram auth, per-service role, `REVOKE` on audit tables | lateral access from web/api compromise | — |
| Supply | images pinned by digest, Trivy scan gate, `bun audit`, `cargo audit`, SBOM per release | known-vulnerable dependencies | 0-day |
| Audit | append-only `audit_log` rows for auth, approval, content publish | silent privilege use | — |

The judge shares a kernel with everything else; that is the accepted cost of `isolate` over a
microVM (ADR 0001). It is mitigated by: no network namespace, no writable host mounts, non-root user
inside the sandbox, `-Werror`-built C++ core, and the API never forwarding attacker-controlled bytes
to the checker.

## Backups and the restore drill

- Nightly `pg_dump -Fc` inside the database's own image (no client install on the host), piped
  through `age` encryption to a temp path, then synced offsite (S3-compatible or a school NAS).
- Key handling: the `age` public key lives in compose; the private key lives **off the server**. A
  backup that cannot be decrypted without the compromised host is not a backup.
- Retention: 7 daily, 4 weekly, 3 monthly. Content (`content/`) is in git — not backed up twice.
- **Restore drill**, run before go-live and monthly after: create a scratch database, restore the
  newest dump, run the row-count assertions, drop it. A backup nobody restored is a hypothesis.

## Deploy path

```
push to master → CI (gates green) → build web/api/judge images → push to GHCR by digest
tag vX.Y.Z → deploy job (SSH, operator key) → docker compose pull + up -d → health check → notify
```

Rollback is by digest: the previous `docker-compose.yml` image digests stay in git, so rollback is a
`git revert` + redeploy, not a rebuild. Deploys are gated on a tag, never on every push.

## Operator checklist (host-level, manual by decision)

Run once on a new box, then never automated — this is the part the "no host scripts" rule reserves
for a human:

1. SSH: key-only, `PermitRootLogin no`, non-default port or tailnet-only.
2. Firewall: allow 80/443 (+ SSH from the operator's network only); nothing else published.
3. `unattended` kernel/OS patching stays a calendar event in the operator's own log, not a cron here.
4. Docker: log rotation set, live-restore enabled, cgroup v2 confirmed (`stat -fc %T /sys/fs/cgroup`).
5. cgroup delegation for `isolate` (`/sys/fs/cgroup/cms-learn-isolate`), same shape as the contest worker.
6. Time sync (chrony) — session expiry and rate-limit windows depend on it.
7. Offsite backup credentials + the `age` public key present; private key absent.

## Open decisions

Asked as one question set; each gets recorded in the ADR once chosen:

1. Database engine — Postgres 18 / Postgres 15 (same as the contest stack) / SQLite.
2. Reverse proxy — Caddy / nginx (as the contest stack uses).
3. Session storage — Postgres rows / Redis / signed cookies only.
4. Edge protection — CrowdSec + CRS AppSec / ModSecurity CRS behind nginx / rate limits only.
5. Transactional email for verification — school SMTP relay / API provider / skip email and rely on teacher approval.

Recommended but not yet asked (they follow from the above, and are recorded as defaults so a later
reader can see them as decisions rather than accidents): judge on the same box with the limits above,
nightly encrypted offsite dumps with a monthly restore drill, Prometheus + Grafana for observability,
tag-gated deploy over SSH from CI, and image digests in git for rollback.
