# 0058 — Every service runs as its own database role

**Status:** accepted, 2026-11-21, by the operator.

## 1. The problem

`deploy/schema/grants.sql` and `deploy/schema/generation.sql` declare what each service may do: `cmslearn_app` for the
api, `cmslearn_judge` for the judge, `cmslearn_generator` and `cmslearn_certifier` for the generation pair. None of it
constrained anything. Every service connected as the same `POSTGRES_USER`, which is the database owner, so every grant was
decoration and a flaw in any service reached every table — the least-privilege work protected nothing because nothing was
running under it.

## 2. The decision

Each service authenticates as a role of its own. The role is new; the grants are not: a login role is a member of
**exactly one** capability group and holds no grant of its own, so what a compromised service reaches is what that group
was granted and nothing else.

| Service | Login role | Member of | What it can reach |
|---|---|---|---|
| api (and the web surface it serves) | `cmslearn_api_user` | `cmslearn_app` | the serving tables; no grant on `generation_certificates` at all |
| generator | `cmslearn_generator_user` | `cmslearn_generator` | jobs, candidates, artefacts, specifications, solutions, programs, cases; **no** certificate |
| certifier | `cmslearn_certifier_user` | `cmslearn_certifier` | `INSERT` on `generation_certificates` and `UPDATE (verification)` on `tasks`; nothing else |
| judge | `cmslearn_judge_user` | `cmslearn_judge` | settings, tasks, examples, visibility, submissions, progress; **not** the bank |
| bank (the seeder) | `cmslearn_bank_user` | `cmslearn_app` | the content tables, through the app group |
| backup | `cmslearn_backup_user` | `cmslearn_backup` | what `pg_dump` needs |

A grant that no group carries is a grant to remove: this record is where that is noticed, because a group is the only
place a grant can live.

One gap this found: `GRANT USAGE ON SCHEMA public` was given to the app and the judge only. The generator and the
certifier had table grants and no schema, which a superuser's `SET ROLE` hides — the owner holds the schema — and a real
login does not. It is granted to every group now.

## 3. The credential

A password never appears in `docker-compose.yml`, in a committed environment file, or in a container's environment: the
environment is readable from `docker inspect` and from anything holding the socket.

Each service gets its password from **a file only that service mounts**, in libpq's own `.pgpass` format
(`deploy/secrets/<service>.pgpass`, gitignored, mode 600), and `PGPASSFILE` names it. The DSN carries the role and no
password: `postgres://cmslearn_api_user@database:5432/cmslearn`.

The environment-variable form was rejected, not overlooked: a settings secret can be sealed (0054) because the service
holds the key when it reads it, and a database password cannot — the service needs it *before* it starts, so there is no
moment at which a key could unseal it.

## 4. The proof

`tools/check-deploy.sh` builds a scratch cluster, applies the plan and `deploy/roles.sql` with test passwords, then makes
each attempt **with the service's own credentials** and prints the role beside the outcome, so a refusal for the api
cannot be mistaken for a refusal from a superuser:

```
  cmslearn_certifier_user allowed
  cmslearn_certifier_user may write a certificate
  cmslearn_certifier_user refused: permission denied for table generation_jobs
  cmslearn_certifier_user is refused a generation job
  cmslearn_certifier_user refused: permission denied for table tasks
  cmslearn_certifier_user is refused any column of a task but its verification
  cmslearn_generator_user refused: permission denied for table generation_certificates
  cmslearn_generator_user is refused a certificate
  cmslearn_judge_user    refused: permission denied for table learner_accounts
  cmslearn_judge_user is refused the bank
  cmslearn_api_user      refused: permission denied for table generation_certificates
  cmslearn_api_user is refused a certificate
```

The separation is attempted, not declared.
