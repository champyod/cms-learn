# 0063 — The schema is applied as desired state, not from a bound plan

**Status:** accepted · **Date:** 2026-11-24 · **Amends:** 0004 (schema flow), keeps its authority rules standing

## Context

ADR 0004 put the schema in one place and made the reviewed artifact the plan: `pgschema plan` writes it,
`pgschema apply --plan` executes it. That was right for the danger it addressed — an edited plan applying
silently — and it carried a defect that only appeared when the deployment was run for real.

A plan is rendered **against a database**. It records that database's state in `source_fingerprint` and
refuses any other, so:

- the migrate step could not run against the deployment's own database, because the committed plan had been
  rendered somewhere else;
- no re-run could work, because the second run's plan differs from the first;
- no upgrade could work, because dropping a column or adding one makes the stored plan stale by definition.

The schema container also has no gateway, so `pgschema` cannot fetch its embedded plan server there. The
wrapper pointed the plan connection at the target database instead, which made the first defect worse.

## The decision

The apply is **desired state**: `pgschema apply --file deploy/plan/0001_init.sql`. The file names the schema
the source declares, and pgschema brings the target to it. A fresh database and an existing one take the
same path, which is what makes `make upgrade` possible at all.

The reviewed artifact stays reviewed: `tools/apply-plan.sh` recomputes the digests against
`deploy/plan/CHECKSUMS` and refuses before connecting when they disagree, and `check:schema` proves the
committed pair still equals a fresh render of `deploy/schema/main.sql`. The plan is still compared, still
digest-checked, and still the only file that reaches a cluster — it is simply no longer bound to the
database it was rendered from.

## Rejected

**Keep `apply --plan` and render per database at deploy time.** It keeps the stronger claim that the
reviewed bytes are the executed bytes, and it makes the deployment depend on a render step that needs the
target's state — the exact coupling that broke the migrate, moved into the thing being deployed.

**Let pgschema plan and apply in one command at deploy time.** Fewer steps, and the reviewed artifact stops
being the thing that runs; a render made seconds before the apply is a render nobody read.

## Consequences

- `check:deploy` asserts the `--file` mode and refuses `apply --plan`, so the bound mode cannot come back
  by accident.
- `check:schema` refuses a plan whose `FOR ROLE` is anything but the deployment's own owner, and applies to
  the cluster the way the deployment does.
- The upgrade path is proven by dropping the two memory columns and applying the same plan: both return
  (ADR 0059).
