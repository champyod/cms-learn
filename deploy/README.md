# deploy/ — one server, and the contract that keeps it safe

Everything here is a **container**; nothing is installed on the host. The two host interactions that
are allowed are read-only mounts (`/proc`, `/sys`, `/`) and the cgroup tree `isolate` needs.

`tools/check-deploy.sh` asserts the contract below on every push, and
`tools/falsify-deploy-gate.sh` proves the gate can fail. If you change a port, a network, an image tag
or the Caddyfile, run both.

## The contract

| # | Invariant | Why it is a gate and not a comment |
|---|---|---|
| 1 | Only `proxy` publishes a port on a public interface (80/443) | every other leak is a service nobody hardened |
| 2 | `database` and `grafana` bind `127.0.0.1` only | operator access through an SSH tunnel, not the internet |
| 3 | Network `internal` is `internal: true` | no gateway means no egress, enforced by the runtime |
| 4 | `judge` is attached to internal networks only | a submission cannot fetch a payload or exfiltrate |
| 5 | Every image is pinned (version or digest) | the thing tested is the thing that runs |
| 6 | `no-new-privileges` everywhere; `judge` is the documented exception | `isolate` needs the host cgroup tree |
| 7 | Caddy validates the Caddyfile **with the binary that will run it** | a config that only parses in the docs is not a config |
| 8 | Prometheus config parses for the pinned Prometheus | same reason |

## First boot (one time, ~15 minutes)

```bash
# 0. DNS: an A/AAAA record for DOMAIN must point at this box before Caddy can issue a certificate.

# 1. configure
cp deploy/.env.example deploy/.env
chmod 600 deploy/.env
# fill: DOMAIN, ACME_EMAIL, POSTGRES_PASSWORD (openssl rand -base64 32),
#       GRAFANA_PASSWORD, AGE_PUBLIC_KEY, CROWDSEC_API_KEY (see step 3)

# 2. the backup key lives on YOUR machine, never here
age-keygen -o cms-learn-backup.key     # prints the age1… public key → AGE_PUBLIC_KEY

# 3. first start (proxy will retry TLS until the record propagates)
cd deploy && docker compose up -d database crowdsec
docker compose exec crowdsec cscli appsec-rules install crowdsecurity/crs
docker compose exec crowdsec cscli bouncers add caddy-proxy -o raw   # → CROWDSEC_API_KEY
docker compose up -d

# 4. verify
curl -fsSI https://$DOMAIN | head -5          # HSTS + no Server header
docker compose exec crowdsec cscli decisions list
docker compose exec crowdsec cscli metrics show appsec
```

`appsec-rules install` is not optional: the AppSec config in `crowdsec/appsec-config.yaml` declares
`crowdsecurity/crs` as **in-band** rules (the request in front of it gets a 403). The stock
`crowdsecurity/crs` appsec-config declares them out-of-band, which only bans the IP for next time —
verified in this repo, the stock config logs `Loaded 0 inband rules`.

## The cgroup delegation `isolate` needs

Same shape as the contest worker, prepared by the operator because it is a host-level change:

```bash
sudo mkdir -p /sys/fs/cgroup/cms-learn-isolate
# delegate to the container's cgroup, exactly as documented for the contest worker
```

`judge` runs with `cgroup: host`, `seccomp:unconfined` and `/sys/fs/cgroup` mounted rw. That is the
one place this stack loosens a default, and the reason ADR 0001 picked `isolate` over a microVM.

## Day-2 operations

| Task | Command |
|---|---|
| Deploy a version | `TAG=v1.2.3 docker compose pull && docker compose up -d` |
| Roll back | restore the previous digest in `.env`, `docker compose up -d` (digests live in git) |
| Manual backup before a migration | `docker compose run --rm -e BACKUP_ONCE=1 backup` |
| Restore drill (needs the private key mounted) | `docker compose run --rm -v /path:/keys:ro -e AGE_KEY_FILE=/keys/cms-learn-backup.key --entrypoint /usr/local/bin/restore-drill.sh backup` |
| Grafana | `ssh -L 3001:127.0.0.1:3001 <host>` then `http://127.0.0.1:3001` |
| Database shell | `ssh -L 5432:127.0.0.1:5432 <host>` then `psql 'postgres://…@127.0.0.1:5432/cmslearn'` |
| Logs | `docker compose logs -f api judge proxy` |

## What is deliberately not automated

Host hardening — SSH keys only, firewall, kernel/OS patching, cgroup delegation, time sync — is an
operator checklist (`docs/infra/single-server.md`). The hub rule is that hosts are operator-manual:
automation that pokes the kernel on a box that also serves a live contest is a risk the operator takes
on purpose, not one a compose file should take silently.

## Not yet real

`web`, `api` and `judge` have no Dockerfiles yet, so `docker compose up` stops at "image not found"
for those three. That is the honest state: the deployment contract, the edge, the WAF, the database,
the monitoring and the backup path are all validated; the three images arrive with their services.
