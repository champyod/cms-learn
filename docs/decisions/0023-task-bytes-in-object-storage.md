# 0023 — Task bytes live in object storage, the row holds the key

Status: accepted 2026-11-11 · decision by the operator

## 1. What the word means here

A task is not one thing. It is information and bytes:

| Part | Examples | Home after this decision |
|---|---|---|
| information | technique, difficulty, topics, limits, titles, locale | a database row |
| bytes | statement text per language, the reference solution, example files, later the test set | an object in a bucket |

"Storage" in this record means the second row only: where the bytes that are not columns live.

## 2. The decision

The bytes live in an **object store**. The database row holds the **key** that names them, never the bytes
themselves:

```
row     task_drafts (id, …, statement_key = 'tasks/alpha/statement.th.md')
object  tasks/alpha/statement.th.md        (the bytes)
```

A reader resolves the key and fetches. No container keeps a copy, and no host directory is mounted.

## 3. Why, and what it costs

The operator's reason: every container stays stateless, nothing of the system lives on a host, and the
bytes can grow without the database carrying them.

The costs are real and are recorded rather than discovered later:

- **A credential inside three containers** — the api uploads, the seeder reads, the content gate
  validates. One secret, three readers, and it must never enter a tracked file.
- **The content gate stops working from a bare checkout.** It must reach the store, so CI needs access
  and a developer without it cannot run the gate.
- **A new failure mode:** a row can name a key whose object does not exist. Every reader must treat that
  as a refusal with a named reason, never as empty content.

## 4. What was rejected

- **A blob table in PostgreSQL.** Cheapest to build, no credential, one backup, and the gate runs
  offline. Rejected because the bytes grow with every task version and the operator wants them out of
  the database.
- **Files in the repository, as today.** The gate sees a draft immediately and the seeder reads it
  unchanged. Rejected because the api would need a writable mount of the checkout, which is exactly the
  host state this decision removes.
- **A row holding the bytes directly (text columns).** Same defect as the blob table, one migration
  earlier.

## 5. What this does not decide

The pipeline that would produce most of the bytes does not exist. This record fixes **where** bytes go
when something writes them; it does not claim anything writes them yet.
