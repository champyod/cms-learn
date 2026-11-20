# 0057 — A hand-written task carries the checks it can

- Status: accepted
- Date: 2026-11-20

## 1. The hole

0046 says a task reaches a learner only on machine evidence, and the platform
enforced that for generated tasks and for nobody else.

A task written by hand ships a reference solution, a statement and example
input and output files. The seeder wrote the content file's status verbatim —
`published` — with verification left `pending` (`bank/src/seed.cpp`), and the
serving queries filtered on status alone (`bank/src/next_task.cpp`, the api's
published-version lookup). Reproduced in the pipeline review: such a row
inserts, and the serving filter selects it. A learner could be given a task no
machine had ever run.

## 2. Why "the same checks as a generated task" is not the answer

The certifier runs three methods (0046 §4): the reference passes the tests, the
reference agrees with a brute-force program on small inputs, and the reference
fits inside half the declared limits.

A hand-written task has no brute-force program. The second method has nothing
to compare against, and inventing a comparison would be a check that passes by
construction.

## 3. The decision

A hand-written task is certified by the checks it can carry: its reference
passes its own examples, and it fits inside half its declared limits.

The certificate says which checks ran and which could not, in the same
`checks` list a generated task's certificate already carries. A reader can then
tell a full certificate from a reduced one without reading the generator's
source.

Publishing becomes a boundary rule rather than a seeder habit:

- a task row is created with a status other than `published`, because the
  certificate's foreign key makes a certificate impossible before the row
  exists (the shape 0046 already uses for verification);
- a move to `published` requires a verified certificate for that task and
  version, in the shape `tasks_verification_needs_its_certificate` already
  enforces for verification;
- the seeder refuses a content file that asks to be published without one, and
  says what to run instead;
- the serving queries require `verification = 'verified'` as well as status, so
  a pending row is never served even if a future writer forgets.

## 4. What this rejects

- **An exemption for hand-written tasks.** It is the one rule the platform was
  built to keep, and an exemption would make "a learner reaches only certified
  work" false in the case most likely to be trusted.
- **Requiring every author to ship a brute-force program.** It would give equal
  evidence and was rejected only because it changes what writing a task means
  for every author forever; a task may still ship one, and then its certificate
  carries all three checks.
- **Trusting the seeder.** The boundary is the row, as everywhere else: a
  guard that lives in one writer is the defect this record exists to close.

## 6. Where a hand-written certificate points

A certificate row required a job, and a hand-written task has none. The
operator chose to name the task and leave the job empty: 0046 already makes the
certificate name the task and version it covers, so the job is provenance
rather than identity, and a link that is present only when a job produced the
row says the truth without a second column to keep in step.

Rejected: a `kind` column beside the link (a second fact that can contradict
the first), and a separate table for hand-written certificates (the same
certificate modelled twice, and every reader deciding which to ask).

## 5. Consequences

- Seeding a published task becomes two steps: create it, certify it, publish
  it. The operator sees the middle step, which is the point.
- A reduced certificate is visibly reduced, so nobody can mistake it for a
  generated task's evidence.
- The content gate can refuse a published file at authoring time, so the
  author learns before a deploy rather than after.
