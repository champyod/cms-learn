# ADR 0003 — single-server deployment, and the security stack it needs

- Status: accepted (2026-10-26)
- Supersedes: nothing. Extends ADR 0001 (scope/stack/sandbox) and ADR 0002 (theme/auth).
- Context: the platform has to run on **one server** for 30–60 students, be operated by one person,
  and be defensible in a school setting where the users are minors. The contest stack (`cms-docker`)
  stays exactly as it is; nothing here shares a database, network or volume with it.

## Decisions

| # | Decision | Chosen | Rejected, and why |
|---|---|---|---|
| 1 | Database | **PostgreSQL 18.6** (`postgres:18.6`) | **15** — the contest stack's version, but nearing the end of its support window for a new project. **SQLite** — zero ops, but one writer: a judge writing verdicts while students write progress contends on one lock. |
| 2 | Reverse proxy | **Caddy 2.11.7** | **nginx + certbot** — proven here, but renewal timers and templated configs are two things the operator must own. **Traefik** — most config surface for no gain at this size. |
| 3 | Edge protection | **CrowdSec 1.8.1 + CRS as *in-band* AppSec rules + IP reputation, decided at the proxy** | **ModSecurity behind nginx** — the bouncer path is Caddy-only. **Rate limits alone** — no WAF, and no IP bans without a host-level fail2ban, which the hub forbids. |
| 4 | Email verification | **Deferred.** Registration is an operator switch (`REGISTRATION_OPEN`); approval is a second switch (`APPROVAL_REQUIRED`). | **School SMTP / transactional API** — both need credentials and a deliverability story before launch; neither is needed to enrol one class. The auth model (ADR 0002) keeps the domain allowlist, so this is a deferral, not a removal. |
| 5 | Hosting | **A VPS the operator controls** (Hetzner-class, 4 vCPU / 8 GB / 80 GB), joined to the tailnet | **School machine** — procurement and an unknown patch cadence. **Managed cloud** — more surface and cost for the same box. Recorded as an assumption, not a purchase: the compose stack does not depend on the provider. |
| 6 | Sessions | **Rows in Postgres**, opaque id in an `HttpOnly; Secure; SameSite=Lax` cookie | **Redis** — one more service to patch and monitor for sub-millisecond reads. **Stateless cookies** — cannot be revoked when a student's device is lost. |
| 7 | Judge placement | **Same box**, in a container with `cgroup: host`, `seccomp:unconfined`, cgroup tree mounted, and **no network gateway** | A second host — not with one server. Shared kernel is the accepted cost of `isolate` (ADR 0001) and is mitigated by the absence of a network namespace and any writable host mount. |
| 8 | Backups | **Nightly `pg_dump -Fc` piped through `age` to an offsite remote**, private key held off-server, retention 7/4/3, monthly restore drill | **Volume snapshots** — provider-specific lock-in and no verification step. **WAL-G/PITR** — right answer for a database with money in it; for a 30–60 student bank, an unverified archive is the bigger risk. |
| 9 | Observability | **Prometheus 3.15.1 + Grafana 13.2.4 + node-exporter 1.8.2**, 30-day retention, judge queue depth as the key metric | Full LGTM stack — three more services for one box. Nothing — a judge that silently stops dequeuing is the failure this must catch. |
| 10 | Deploy path | **Images by digest, deploy gated on a tag, `compose pull && up -d`, rollback by digest** | Deploy on every push — a broken commit becomes a broken lesson. Rebuild-on-server — the box stops being immutable. |

## Consequences

- **The judge is reachable by the database and nothing else.** It cannot fetch a payload; a submission
  that needs the network fails by design.
- **A stolen dump is not a stolen lesson plan._ The archive is encrypted with a public key, and the
  private key is never on the server, so host compromise does not hand over history.
- **The WAF blocks in-band.** Verified: the stock `crowdsecurity/crs` appsec-config reports
  `Loaded 0 inband rules`, which only bans the IP for later requests. `deploy/crowdsec/appsec-config.yaml`
  declares CRS as in-band and logs `Loaded 1 inband rules` with `Appsec listening on 0.0.0.0:7422`.
- **No email means the domain allowlist matters more**, not less: it is the only thing tying an account
  to the school. `ALLOWED_EMAIL_DOMAINS` empty is acceptable only while approval is on.
- **Host hardening stays manual.** Firewall, SSH, kernel patching, cgroup delegation and time sync are
  an operator checklist. Automating them would mean a container with host write access to the kernel
  that also serves a live contest.

## Evidence this ADR relies on (all run in this repo, not asserted)

| Claim | How it was checked |
|---|---|
| The proxy image builds and contains both modules | `docker build deploy/proxy` → the Dockerfile's own `caddy list-modules` guard passes |
| The Caddyfile is valid for that binary | `caddy validate` inside the built image → `Valid configuration` |
| Compose interpolates and structures | `docker compose config -q` → OK; 10 services |
| Only the proxy is public; db/grafana loopback | `docker compose config --format json` → asserted in `tools/check-deploy.sh` |
| The deploy contract can fail | `tools/falsify-deploy-gate.sh` → **7/7** cases caught, each with its own reason |
| Backups restore, not just dump | `pg_dump \| age` against Postgres 18.6, then `restore-drill.sh` → 250 users / 1000 submissions restored into a scratch DB, live DB untouched |
| A corrupt archive fails the drill | random-bytes archive → drill exits 1 |
| A dump cannot be written unencrypted | `run.sh` without `AGE_PUBLIC_KEY` → exits 1 |
| The drill refuses to target the live DB | `SCRATCH_DB=cmslearn` → refuses, exit 1 |
| CrowdSec loads in-band CRS and listens on 7422 | `Loaded 1 inband rules`, `Appsec listening on 0.0.0.0:7422` |
| Prometheus config parses for the pinned version | `promtool check config` → SUCCESS |
| The Discord notify path executes | manual dispatch with `simulate_failure=true` → job ran, printed the missing-secret notice |

## Open

- `web`, `api`, `judge` images do not exist yet; the compose file references them and `up` stops there.
- The hosting provider is an assumption until a box exists.
- WAL archiving/PITR is the upgrade path when the data becomes worth losing an hour of.
