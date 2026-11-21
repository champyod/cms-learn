# deploy/ — one server, and the contract that keeps it safe

Everything here is a **container**; nothing is installed on the host. The two host interactions that
are allowed are read-only mounts (`/proc`, `/sys`, `/`) and the cgroup tree `isolate` needs.

`tools/check-deploy.sh` asserts the contract below on every push, and
`tools/falsify/cases/deploy.ts` proves the gate can fail. If you change a port, a network, an image tag
or the Caddyfile, run both.

## How `docker-compose.yml` is produced

`docker-compose.yml` is generated. It is never edited by hand.

```
release.toml                     what runs — the version and, per image, its kind
deploy/stack.template.yml        the shape — services, networks, and why each one exists
        │
        └── tools/render-stack.ts ──→ deploy/docker-compose.yml
```

`release.toml` declares each image as one of three kinds:

| Kind | Carries | Renders as | Meaning |
|---|---|---|---|
| `pinned` | `ref` + `digest` | `ref@digest` | a registry image; the digest is what runs |
| `built` | `ref` + `context` | `ref:${TAG:-dev}` | built from a Dockerfile in this repository |
| `unpublished` | `ref` | `ref:${TAG:-dev}` | published by CI and not yet existing; the release is not deployable |

```bash
bun run tools/render-stack.ts          # write docker-compose.yml
bun run check:stack                    # verify the committed file is what the sources produce
bun run check:stack:falsify            # prove that check can fail
```

`bun run gate:deploy` runs the stack gate before the contract gate, so a hand edit is reported as drift
with a line number instead of being silently reverted on the next render.

## How an upgrade runs

One command, and it either finishes or leaves the previous release serving:

```bash
make upgrade          # tools/upgrade.sh
```

| Order | Step | What it refuses to proceed past |
|---|---|---|
| 1 | the committed stack is what the sources render | a hand-edited `docker-compose.yml`, or a release whose `release.toml` disagrees with it |
| 2 | the previous release is recorded | — (nothing to refuse; this is what a rollback needs) |
| 3 | `compose pull --ignore-buildable` | an image that cannot be fetched at the digest the release names |
| 4 | `compose --profile migrate run --rm schema` | a plan whose digests do not match `deploy/plan/CHECKSUMS`, or a migration the database rejects |
| 5 | `compose up -d --wait` | a container that does not become healthy |
| 6 | `GET https://$DOMAIN/healthz` | a release that starts but does not serve |

Nothing is replaced before step 5, so a failure in 1–4 costs a pull and nothing else. A failure in 5 or
6 restores `deploy/.previous-compose.yml`, which is the stack that was running before this command
started; the two record files are gitignored because they describe this server, not the repository.

`bun run check:upgrade` drives the script with stubbed `docker`, `bun`, `curl` and `sleep`, so the order
and the rollback are asserted without a daemon or an image; `bun run check:upgrade:falsify` breaks the
script six ways — no health gate, a rollback that does nothing, no rollback when the switch fails,
migration after switch, no release record, a swallowed migration failure — and requires the gate to
name each one.

## The contract

| # | Invariant | Why it is a gate and not a comment |
|---|---|---|
| 1 | Only `proxy` publishes a port on a public interface (80/443) | every other leak is a service nobody hardened |
| 2 | `database` and `grafana` bind `127.0.0.1` only | operator access through an SSH tunnel, not the internet |
| 3 | Network `internal` is `internal: true` | no gateway means no egress, enforced by the runtime |
| 4 | `judge` is attached to internal networks only | a submission cannot fetch a payload or exfiltrate |
| 5 | Every third-party image is pinned by digest, from `release.toml` | the thing tested is the thing that runs |
| 6 | Every service runs with a read-only root filesystem, drops every capability, adds back exactly the ones its job needs, and sets `no-new-privileges` — the judge too, beside its documented seccomp exception | a service that can write its own rootfs can replace the binary it runs; Docker's default capability set is authority nobody asked for (ADR 0060) |
| 7 | Every writable path inside a container is a named volume or a bounded tmpfs, and every bind mount is read-only except the judge's cgroup tree | a read-only rootfs with an unnamed writable directory is a promise, not a boundary |
| 8 | Every service has a memory, CPU and process limit | one service's leak or fork bomb must not take the machine from the rest |
| 9 | Caddy validates the Caddyfile **with the binary that will run it** | a config that only parses in the docs is not a config |
| 10 | Prometheus config parses for the pinned Prometheus | same reason |

## First boot (one command)

```bash
make first-run          # tools/first-run.sh
```

It renders the stack, generates the secrets the roles read, creates the roles, applies the plan, starts
CrowdSec, installs the in-band CRS rules, obtains the bouncer key, brings every service up and waits
for `https://$DOMAIN/healthz` to answer. Running it twice converges: it never rewrites a secret, never
replaces an existing bouncer, and the plan is idempotent.

The facts it cannot invent are the operator's: `DOMAIN`, `ACME_EMAIL`, `AGE_PUBLIC_KEY` and
`CMSLEARN_BOOTSTRAP_ADMIN`. Every one that is missing is named in a single refusal, together with an
unpublished image in `release.toml` or a host without docker, before anything on the host is changed:

```bash
cp deploy/.env.example deploy/.env
cp deploy/host.env.example deploy/host.env
chmod 600 deploy/.env deploy/host.env
```

`host.env` is gitignored: it carries the seal key and belongs to this server only. The backup key is
generated on your machine, never here (`age-keygen -o cms-learn-backup.key`); its public half is
`AGE_PUBLIC_KEY`.

Everything else is generated once and recorded where its reader is: role passwords in
`deploy/secrets/<service>.pgpass` (mode 600, mounted only into that service), the CrowdSec bouncer key
and the operator-visible passwords in `deploy/.env`, and the seal key in `deploy/host.env`. A role that
exists without its file is refused rather than given a new password, because the old one cannot be
recovered.

DNS comes first: an A/AAAA record for `DOMAIN` must point at this box before Caddy can issue a
certificate. The first run waits for the health endpoint, so a missing record is a named failure, not a
stack that looks up but does not serve.

Verify what it started:

```bash
curl -fsSI https://$DOMAIN | head -5          # HSTS + no Server header
curl -fsSI https://$DOMAIN/admin | head -5    # the panel, behind the same edge
docker compose exec crowdsec cscli decisions list
docker compose exec crowdsec cscli metrics show appsec
```

`bun run check:deploy` asserts the target exists and drives `tools/check-first-run.sh`, which proves the
order, the secrets' placement and every refusal with stubbed docker and curl, so the command is covered
by the deployment contract rather than by this page.

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

`judge` runs with `cgroup: host`, `seccomp:unconfined` and `/sys/fs/cgroup` mounted rw, and it adds
eight capabilities back for `isolate` (`SYS_ADMIN`, `SYS_CHROOT`, `SETUID`, `SETGID`, `CHOWN`,
`DAC_OVERRIDE`, `FOWNER`, `NET_ADMIN`, each with its reason in ADR 0060); `database` adds the four and
`objects` the two their entrypoints need, and every other service adds none. `no-new-privileges` still
holds in the judge. That is the one place this stack loosens a default, and the reason ADR 0001 picked
`isolate` over a microVM.

## Day-2 operations

| Task | Command |
|---|---|
| Deploy a version | `make upgrade` — see above |
| Roll back | automatic when the switch or the probe fails; to return to an older release on purpose, `git revert` the commit that changed `release.toml`, `bun run tools/render-stack.ts`, then `make upgrade` |
| Manual backup before a migration | `docker compose run --rm -e BACKUP_ONCE=1 backup` |
| Restore drill (needs the private key mounted) | `docker compose run --rm -v /path:/keys:ro -e AGE_KEY_FILE=/keys/cms-learn-backup.key --entrypoint /usr/local/bin/restore-drill.sh backup` |
| Administrator panel | `https://$DOMAIN/admin` — proxied to the api's panel listener (8081), never published itself |
| Grafana | `ssh -L 3001:127.0.0.1:3001 <host>` then `http://127.0.0.1:3001` |
| Database shell | `ssh -L 5432:127.0.0.1:5432 <host>` then `psql 'postgres://…@127.0.0.1:5432/cmslearn'` |
| Logs | `docker compose logs -f api judge proxy` |

## What is deliberately not automated

Host hardening — SSH keys only, firewall, kernel/OS patching, cgroup delegation, time sync — is an
operator checklist (`docs/infra/single-server.md`). The hub rule is that hosts are operator-manual:
automation that pokes the kernel on a box that also serves a live contest is a risk the operator takes
on purpose, not one a compose file should take silently.

## Not yet real

`api` and `judge` have no published image yet, so `docker compose up` stops at "image not found" for
those two. That is the honest state: the deployment contract, the edge, the WAF, the database, the
monitoring and the backup path are all validated; the images arrive from CI.

The `api` image carries the three trees it renders — templates, static files and the settings
catalogue — at the paths the binary resolves, so the only thing `stack.template.yml` mounts into it is
the operator's host file. `tools/check-deploy.sh` asserts that: a bind mount of anything else into
the api fails the gate.

The api bootstraps its own first run: it creates the `system:bootstrap` identity the control socket acts
through (silently when it already exists) and, on a database that holds no administrator, the first one
from `CMSLEARN_BOOTSTRAP_ADMIN`, printing the handle once. A database that already holds an administrator
is left untouched. The first administrator is created without a password, and nothing in this tree sets
one on an existing account yet — so a fresh install reaches a running api and a reachable panel with
nobody able to sign in as an administrator. That credential path is the open item.
