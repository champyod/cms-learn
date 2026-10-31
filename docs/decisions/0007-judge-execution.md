# 0007 — How a submission reaches the sandbox

## Context

A submission has to be compiled, run under limits, and checked, without the contestant's program being
able to touch anything else. `isolate` (IOI, v2.7) is the sandbox: it starts the program inside namespaces,
enforces CPU time, wall time, address space and process count through cgroups, and writes a report. It needs
root, or the capabilities that stand in for root, because creating those namespaces is privileged.

That fixes two things and leaves one open. Fixed: the sandbox is a separate process that the judge forks,
and a "box-id" names a per-run slot that isolate maps to a UID range. Open: how many judge containers exist,
how a run is assigned to one, and where the shard boundary sits.

## Options considered

**A — one container, a worker pool inside it, boxes shared across runs.** Every run takes a free box-id from
a pool the process maintains. Fewest moving parts, one image, one log stream. But boxes become a global
resource that code has to track, two runs can contend for one id if the pool count and the isolate
configuration drift apart, and every run shares one root process tree.

**B — one container per shard, each with a fixed box range.** Strong kernel separation between shards and a
blast radius of one shard. But the shard count is a deployment shape that has to be edited per copy, a
stalled shard is invisible from outside, and each shard idles when it has no work.

**C — N identical workers, a job queue in Postgres, local box-ids.** Each worker claims a job row with
`FOR UPDATE SKIP LOCKED`, uses box-ids `0..K` locally, and reports the verdict back to the same row. The
shard count is the replica count. A worker holds no identity of its own, so losing one loses nothing but an
expired claim.

## Decision

**C.** The submission row is already the unit of work, Postgres is already the source of truth, and
`box-id` is already a per-host resource in isolate's own design — so there is no new notion to introduce,
only a claim query and a lease.

The claim:

```sql
UPDATE submissions SET claimed_by = $1, claimed_at = now()
WHERE id = (SELECT id FROM submissions
            WHERE verdict IS NULL AND (claimed_at IS NULL OR claimed_at < now() - interval '5 minutes')
            ORDER BY id
            LIMIT 1
            FOR UPDATE SKIP LOCKED)
RETURNING id;
```

The `claimed_at` age check is the lease: a worker killed mid-run leaves a row that becomes claimable again,
rather than a submission that silently never runs.

## Consequences

- The replica count is the only thing that changes when more throughput is needed.
- A worker never needs to know which shard it is; it needs to know which box-ids it may use, which is a
  settings value (`judge.box_count`), not a deployment fact.
- The supervisor's `SIGTERM` → `SIGKILL` escalation is what makes the lease safe to rely on: a run cannot
  outlive its worker by much, because the worker kills its own process group on the way down.
- Deleting a worker mid-run costs at most one re-run of one case, which is cheaper than any coordination
  protocol that would prevent it.
