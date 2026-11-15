# 0032 — A decision belongs to a job, and a job belongs to one task version

Status: accepted 2026-11-16 · decision taken while building the queue, recorded because it existed only as a comment

## 1. The rule

**One job per task version.** A second job for the same version would repeat every model call and write a
record nobody could tell from the first, so a re-run is a **new task version** — which is what a version is
for.

`generation_jobs` carries `UNIQUE (task_id, task_version)` and the database refuses the second job, naming both.

## 2. Why it is a record rather than a schema comment

The sentence lived in `deploy/schema/generation.sql` as a comment next to the constraint. A rule that decides
behaviour and exists only in a comment is the thing this repository keeps losing: an agent rewriting the
comment as "one job per task" would change the meaning without changing a line of SQL.

The rule also answers a question a reader will ask: **what happens when a run must be repeated?** The reply
is not "insert another row" but "make a new version", and that is a decision about the data model, not an
implementation note.

## 3. The shape that followed

| Fact | Where it lives | What refuses it |
|---|---|---|
| one decision per job | `generation_candidates` numbering with no gap and exactly one chosen | a deferrable constraint trigger reading the whole decision at commit |
| one call row per model call | `generation_artefacts` | `UNIQUE NULLS NOT DISTINCT (job_id, step, kind, locale, call_index)` |
| a candidate names the call that made it | `generation_candidates.call_artefact_id` | `NOT NULL`, plus a foreign key `ON DELETE NO ACTION` |
| an evidence row cannot be deleted while cited | the same foreign key | `NO ACTION` and a trigger naming the citing decision: *"artefact N is the call that produced position P of generation job J; the decision cannot lose the call that made it"* |

## 4. Why `NO ACTION` and not `RESTRICT`

Both refuse a lone delete of a cited artefact. They differ when a **job** is deleted: the job cascades into
artefacts and candidates in one statement, and `RESTRICT` checks immediately, so it can reach an artefact
before the candidate that cites it and refuse a delete that is legitimate. `NO ACTION` checks at the end of
the statement, when both are gone — so a job delete succeeds and a lone artefact delete is refused.

## 5. Consequence worth stating

`call_artefact_id` is `NOT NULL` and it landed while `generation_candidates` was empty, so no backfill was
needed and no row could violate it. A future reader adding the same column to a populated table owes the
opposite: a backfill, and a two-step migration rather than one.
