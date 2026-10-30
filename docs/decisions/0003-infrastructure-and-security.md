# 0003 — single-server deployment and the security stack

Status: accepted 2026-10-26 · extends 0001 and 0002 · the contest stack is untouched and shares no
database, network or volume

| # | Decision | Chosen | Why |
|---|---|---|---|
| 1 | Database | **PostgreSQL 18.6** | three processes write concurrently; the settings path needs server-side notification and row-level security |
| 2 | Reverse proxy | **Caddy 2.11.7** | certificates and reloads are configuration, not timers to own |
| 3 | Edge protection | **CrowdSec 1.8.1**, CRS as *in-band* AppSec rules, IP reputation decided at the proxy | a WAF that only bans for later requests is not a WAF |
| 4 | Email verification | **Deferred**; `REGISTRATION_OPEN` and `APPROVAL_REQUIRED` are settings rows, and the known-organisation list decides the `external` mark rather than refusing signup | neither credential nor deliverability is needed to enrol one class; approval is the barrier, so it defaults on |
| 5 | Hosting | **A VPS the operator controls**, 4 vCPU / 8 GB, joined to the tailnet | the compose stack does not depend on the provider |
| 6 | Sessions | **Rows in Postgres**, opaque id in an `HttpOnly; Secure; SameSite=Lax` cookie | a lost device must be revocable |
| 7 | Judge placement | **Same box**, container with `cgroup: host`, `seccomp:unconfined`, cgroup tree mounted, **no network gateway** | one server; the judge is reachable by the database and nothing else |
| 8 | Backups | **Nightly `pg_dump -Fc` through `age` to an offsite remote**, key held off-server, 7/4/3, monthly restore drill | an unverified archive is the bigger risk at this size |
| 9 | Observability | **Prometheus 3.15.1 + Grafana 13.2.4 + node-exporter 1.8.2**, 30-day retention, judge queue depth as the key metric | a judge that silently stops dequeuing is the failure to catch |
| 10 | Deploy path | **Images by digest**, `up -d --wait`, rollback by digest | a broken commit must not become a broken lesson |
| 11 | Proxy privilege | **uid 10001 on unprivileged ports** (80→8080, 443→8443) | the process terminating TLS does not need root |
| 12 | Security scanning | **Trivy 0.66.0 as a gate**: secrets, fixable CVEs, deployment hardening | a leak fails the build instead of waiting to be read |
| 13 | Object storage | **SeaweedFS 4.51**, Apache-2.0, one `weed server -s3` process; reached by **aws-sdk-cpp** with `default-features: false` and only the `s3` feature | task text, source and test data are blobs, not files on a host; the database holds pointers, so the bucket is the only copy and the backup covers it |

## Consequences

- The judge has no network gateway; a submission that needs the network fails by design.
- **A backup is two artefacts, not one.** Task text, source and test data live in the object store;
  the database holds only pointers. A `pg_dump` restores the rows and leaves every task unreadable, so
  the nightly job covers the bucket as well, and the restore drill restores both before it reports
  success.
- **A private object has no URL of its own.** Public assets are served under `content.assets_path`,
  a prefix the api owns so an operator can change it from the panel without reloading the proxy; a
  private object is streamed through the api after the same capability check that guards the page
  that links to it. No presigned URL is minted, because a presigned URL is a bearer token that keeps
  working after the session that requested it is gone.
- A stolen dump is not a stolen lesson plan: the private key is never on the server.
- The WAF blocks in-band — the stock CRS config reports `Loaded 0 inband rules`, which only bans for
  later requests, so `deploy/crowdsec/appsec-config.yaml` declares CRS in-band and logs
  `Loaded 1 inband rules`.
- With no email, the domain allowlist matters more: it is the only thing tying an account to the
  school. An empty allowlist is acceptable only while approval is on.
- Host hardening stays manual — firewall, SSH, kernel patching, cgroup delegation, time sync. It is an
  operator checklist, not a container with write access to the kernel.

## Verified

| Claim | Check |
|---|---|
| The proxy image builds with both modules | `docker build deploy/proxy`, whose `caddy list-modules` guard passes |
| The Caddyfile is valid for that binary | `caddy validate` → `Valid configuration` |
| Only the proxy is public | `docker compose config --format json`, asserted in `tools/check-deploy.sh` |
| The deploy contract can fail | `tools/falsify-deploy-gate.sh` → 7/7 caught |
| Backups restore, not just dump | `pg_dump \| age` then `restore-drill.sh` → 250 users / 1000 submissions into a scratch DB |
| A corrupt archive and an unencrypted dump both fail | random bytes → exit 1; no `AGE_PUBLIC_KEY` → exit 1 |
| The drill refuses the live database | `SCRATCH_DB=cmslearn` → exit 1 |
| CrowdSec loads in-band CRS | `Loaded 1 inband rules`, `Appsec listening on 0.0.0.0:7422` |
| Prometheus config parses | `promtool check config` → SUCCESS |
| The scan finds nothing and can fail | `security-scan.sh` green; `falsify-security-gate.sh` caught 4/4 planted defects |

## Open

- `web`, `api` and `judge` images do not exist yet; compose references them and `up` stops there.
- The hosting provider is an assumption until a box exists.
- WAL archiving and PITR replace the nightly dump when the data is worth an hour of loss.
