# 0026 — The object store requires a key

Status: accepted 2026-11-11 · decision by the operator

## 1. What the stack did before

`deploy/stack.template.yml` runs the object store with `server -s3 -dir=/data`, no credentials, on the
`internal` network with nothing published. Its own comment calls this "the documented allow-all state,
and the network below is the boundary".

Every container on that network could therefore read and overwrite **every task byte**. One of those
containers runs untrusted student code.

## 2. The decision

The store requires an access key and a secret. They live in the gitignored host file, beside the seal key,
and reach the containers as configuration. They never enter a tracked file, a log, a URL or a compose
`environment:` value.

Holders, and only these:

| Reader | Why |
|---|---|
| `api` | uploads what a setter saves, fetches what a page shows |
| the seeder | imports published bytes into the database |
| the content gate | validates a task against the store |

## 3. What was rejected

- **Keep the network as the boundary.** Nothing to configure, and one sandbox escape in the judge turns
  into read-write access to the whole task bank.
- **One shared key for everything.** Simplest rotation, and a leaked key is indistinguishable from a
  legitimate reader in the audit trail.
- **Per-service keys issued by the store.** The strongest reading, and it needs an identity and a policy
  management path that this repository does not have yet; recorded here as the natural next step rather
  than built now.

## 4. Consequences

- `check:deploy` gains an assertion that the key names exist and their values are empty in the tracked
  example files, and that the compose file requires them.
- The content gate now needs the store. A developer without the credential cannot run it, and CI must
  hold it — this cost was accepted in `0023` and is restated here because it is now enforced.
- Rotation is a host-file edit and a restart; nothing else changes.
