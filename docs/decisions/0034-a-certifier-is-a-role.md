# 0034 — A certifier is a role, and its grant is one column

Status: accepted 2026-11-19 · raised by the data reviewer while building step 7

## 1. The question

The certifier needed to write `tasks.verification`, and the generator is forbidden from writing anything on
`tasks`. The reviewer asked: is `GRANT UPDATE (verification) ON tasks TO cmslearn_certifier` a trade of the
evidence barrier, or the narrowest form of it?

**It is the narrowest form.** The barrier these records defend is not "no role may write verification". It
is: *the role that generates a task is not the role that certifies it.* A second role, with one writable
column, keeps that separation and adds no power to the generator.

## 2. Two independent guards, not one

| Guard | What it refuses |
|---|---|
| **privilege** | `cmslearn_generator` holds only column-scoped `SELECT` on `tasks`; it cannot write verification, status or content at all |
| **the trigger** | `tasks_verification_needs_certificate` refuses any verification change with no `generation_certificates` row whose outcome matches, and names `cmslearn_certifier` and both grants |

Drop either and the other still holds. `tools/check-pipeline.sh` drives both: it attempts the write as the
owner, then as the certifier, and a live case drives it as the generator and gets
`permission denied for table tasks`.

## 3. Why not a `SECURITY DEFINER` function

Offered by the reviewer as the alternative. It **moves** the authority rather than narrowing it: the
function's owner can write whatever the owner can write, so the safety of the whole barrier then depends on
every future edit to one function body being correct. A column grant is enforced by the database for every
statement, including ones nobody has written yet.

## 4. What the certifier may do

| May | May not |
|---|---|
| read the evidence rows of steps 3, 5 and 6 | write a task's content, title, limits or status |
| write a `generation_certificates` row | publish a task |
| set `tasks.verification` to `verified` or `failed` | certify a task whose evidence is missing, stale or disagrees with its digest |

A refusal writes **nothing**: verification stays `pending` and no certificate row exists, so "no certificate"
and "certified" are distinguishable by a query rather than by a convention.

## 5. The rule this leaves behind

**A role earns exactly the columns its job needs, and a second guard covers the same act.** One guard is a
convention; two are a barrier.
