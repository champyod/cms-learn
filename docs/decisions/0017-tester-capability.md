# 0017 — An administrator who tests problems acts as themselves

Status: accepted 2026-11-08 · decision by the operator

## 1. The two surfaces stay separate

A learner session opens learner pages. An administrator session opens admin pages. A learner can never
reach an admin page, because a learner holds no capability at all (`0016-learner-and-admin-accounts.md`).

One exception exists, because a problem must be checked before students meet it: an administrator
holding the `tester` capability may open the learner pages.

## 2. A test submission belongs to the administrator who made it

The exception is about *reach*, not about *identity*. An administrator who submits a solution while
testing:

- is recorded as the submitter, under their own administrator account;
- does not change any learner's record, rank or progress;
- does not appear in a learner's history, and does not count as a learner having seen the task.

The alternative — the administrator borrowing a learner's identity so that the result matches what a
student would see — was rejected. It looks closer to reality and it is bought by letting an
administrator silently change a minor's record, which is the failure this project's audit trail exists
to prevent.

## 3. How the serving path represents this

The serving layer builds an *audience* (the list of people a task may be shown to) from a learner's
`serial_id`. An administrator has no such number, so:

- a learner session composes its audience from `learner_accounts.serial_id`, exactly as before;
- an administrator session composes **no audience**, and reaches a task through the `content:read`
  capability branch that already exists (`0010-task-visibility.md` §5), which is the same branch that
  lets an administrator read a hidden task;
- any other account kind is refused rather than defaulted to "everyone".

Refusing rather than defaulting is the point: the dangerous version of this code is the one that has to
produce *some* audience for an account that has no number, and the cheapest wrong answer is "everyone".

## 4. Naming

The capability is `tester`: it grants entry to the learner pages from an administrator session. It does
not grant "acting as a learner", and no code path should be named that way — a name that suggests
impersonation invites the behaviour back.

## Consequences

- `bank/src/visibility.cpp` stops taking a bare principal id and takes the account kind with the id.
  A learner gets an audience; an administrator gets none; anything else is `invalid_argument`.
- One new capability joins the enumeration, so `deploy/schema/types.sql`, the capability count in the
  gates and the role that carries it all change together.
- The submission path records an administrator's test with their administrator id, so
  `submissions.account_id` now references a learner *or* an administrator. That is the second place the
  account-kind column is needed, and it is the same column `audit_events` already carries.
- Not decided here: whether a test submission is hidden from a public verdict list, and how long a
  tester's submissions are kept. Both are separate questions.
