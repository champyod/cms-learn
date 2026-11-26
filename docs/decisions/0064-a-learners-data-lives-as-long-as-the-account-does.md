# 0064 — A learner's data lives as long as the account does

**Status:** accepted · **Date:** 2026-11-26 · **Decides:** the retention window F-055 names

## Context

F-055 promises "PII minimisation and retention for minors" and no window was written, so nothing expires
and nothing says how long anything lives.

The two judges this platform is measured against say the same thing in different words. Codeforces keeps
data *"for the time necessary to provide the service … and the User can always request that the Data
Controller … remove the data"*; AtCoder *"will not retain … beyond the period necessary … When personal
data is no longer necessary, we will delete the data promptly."* Neither promises a number, and neither
promises deletion only when an account is closed. Both pair a necessity rule with a right to erasure.

A fixed window was considered — one academic year, enough for a late grade appeal — and **rejected by the
owner**, who chose the account's lifetime instead: a learner's history is theirs to keep for as long as
they hold the account, and closing the account is what ends it.

## The decision

**A learner's data lives exactly as long as the account does.** Closing the account is the erasure
mechanism: the schema already cascades from `learner_accounts` to `submissions`, so deleting the account
row deletes the programs with it, in one statement the database guarantees rather than a job that could
skip a row.

Three obligations follow, and they are the feature:

1. **A deletion path exists.** An administrator can close an account, and the action names the account in
   the audit trail, because "we deleted it" with no record of who asked is a promise nobody can check.
2. **The notice says it.** The catalogue carries the rule in the words a guardian reads: *kept while the
   account exists, deleted with it, on request at any time* — not a euphemism and not a silence.
3. **Nothing keeps a copy elsewhere.** An artefact, a backup or a log that outlives the account would make
   the promise false, so the rule covers the store and the backups the deployment already rotates.

## Rejected

**One academic year, then delete automatically.** It guarantees that an abandoned account stops holding a
minor's code, and it costs a sweep job, a settings window and a notice that says two things at once. The
owner preferred the simpler promise, and the simpler promise is the one a school can explain.

**No stated rule at all.** What the platform effectively had before this decision: data lives until
somebody remembers. It is the one option no notice can carry.

## Consequences

- F-055 is no longer a retention sweep. It is a **deletion path plus the notice**, and the row says so.
- The cascade is the enforcement; a test must prove that deleting an account deletes its submissions rather
  than leaving rows behind, because a promise enforced by a foreign key is only as good as the key.
- Self-service deletion is a separate decision: today an administrator does it, which is the shape a school
  with minors wants, and a learner doing it themselves would be a different promise.
