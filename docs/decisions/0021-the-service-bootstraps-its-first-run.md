# 0021 — The service bootstraps its own first run

Status: accepted 2026-11-10 · decision by the operator

## 1. The write nobody could perform

`cms::api::bootstrap` existed, was tested (`api/tests/bootstrap_test.cpp`), and had no caller. A fresh
database therefore could not reach a running api by any supported route:

- the control socket refuses to start unless `system_accounts` holds `system:bootstrap`
  (`api/src/admin_socket.cpp` resolves its caller by that handle at construction), and
- `CMSLEARN_BOOTSTRAP_ADMIN` was read into the host config and never used, and
- the operator had no command that creates an account: the panel needs a signed-in administrator, and
  `cmsctl account create-admin` needs the socket the missing row blocks.

Every gate worked around it by inserting the row by hand. An installation could not, so the deployment
contract described a stack that cannot boot. That is what this record fixes.

## 2. Two facts, ensured separately, in this order

A start ensures both facts, each on its own:

| Order | Fact | When absent | When present |
|---|---|---|---|
| 1 | `system_accounts` holds `system:bootstrap` | created, and the id is returned | read and reused; never a second row |
| 2 | `admin_accounts` holds at least one administrator | created from `CMSLEARN_BOOTSTRAP_ADMIN`, handle printed once | nothing written, nothing printed |

The order is load-bearing. The two facts are independent: a database can hold an administrator and no
system identity — every registered shell test was in exactly that state — and one call that refuses on
"this database already holds administrators" would create neither, leaving the socket dead with the
failure this record exists to remove.

So "already bootstrapped" is not an error and not a branch the caller has to read: `ensure_first_run`
returns `false` and writes nothing. `ErrorCode::conflict` appears only when two processes bootstrap the
same database at once: both see no administrator, one insert wins and the losing start refuses with the
driver's message. One api serves a stack, so that residue is accepted rather than engineered around with
a lock this stack has no other use for.

## 3. The handle is derived from the address, not a second key

`CMSLEARN_BOOTSTRAP_ADMIN` is already a required host-file fact, so the first administrator's handle is
its local part: lowercased, every character outside `a-z 0-9 . _ -` replaced with `-`, leading
non-alphanumerics removed, truncated to 64. `operator@example.org` becomes `operator`.

The handle is validated on **every** start, whether or not an administrator has to be created, because
every other host-file fact is: a value that cannot become a legal handle refuses the start and names
`CMSLEARN_BOOTSTRAP_ADMIN`, rather than failing months later on the day the database is emptied.

Rejected:

- **a `CMSLEARN_BOOTSTRAP_HANDLE` key.** Two operator facts for one identity, which would then have to be
  kept in step, and every existing host file and fixture would need the new key before the service starts.
- **a default handle** such as `admin`. A name an operator did not choose is a name they will not
  recognise in the audit trail.
- **a password in the host file.** A secret the operator has to invent and rotate belongs to a credential
  decision (see §5), not to the fact that an account exists.

## 4. What the start says, and what it does not

One line, printed once, on the start that creates the administrator:

```
cms-api: created the first administrator 'operator'; it holds no password yet
```

Nothing on a database that already holds one. The line is flushed before the listeners start: under
`docker logs` stdout is a file, and an unflushed line would appear only when the process ended.

The audit row is `bootstrap.completed`, with `system:bootstrap` as the actor and the new administrator as
the subject — the same shape `0020` requires of every write, so the one write the service performs on its
own is attributable like the others.

## 5. The administrator has no password — the open item

`admin_accounts` carries no email column (`0016`), and nothing in the tree sets a credential on an
existing account:

- the panel's create-administrator form sets one, but needs a signed-in administrator;
- the socket's `account create-admin` sets one, but `cmsctl` is not in the api image.

So the first administrator holds the audit trail and the panel has nobody who can sign in as it. This
record does not hide that: the deployment documents state it, and a credential path — a host-file secret
hashed at creation, or `cmsctl` shipped in the image — is the next step rather than a detail this step
left undone.

## 6. What this does not change

- No default password, no printed secret, no account beyond the first: later administrators are created by
  the panel or the socket, which already have the authority for it.
- `bootstrap` keeps its once-only form and its refusal, because the live case states the rule with it; the
  service calls `ensure_first_run` instead, for which a populated database is the steady state.
- The migrations are unchanged. No schema change, no new table, no seed: the write belongs to the service,
  so it happens once per database rather than once per release.

## Consequences

- A fresh database reaches a running api from one host-file line (`CMSLEARN_BOOTSTRAP_ADMIN`), with no SQL.
- The gates stop seeding `system:bootstrap` by hand: six shell tests and the render gate now run against
  the automatic first run, which is what makes them evidence instead of a re-enactment of a manual step.
- The manual `INSERT` documented by `0016`'s step and by the deployment runbook is removed from both.
- `deploy/host.env.example` says what the address now does, and that the account arrives without a
  password.
