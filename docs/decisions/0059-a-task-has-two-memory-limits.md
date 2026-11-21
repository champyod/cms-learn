# 0059 — A task has two memory limits

- Status: accepted
- Date: 2026-11-21

## 1. The problem

The platform measured memory two ways and treated the answers as one.

The certifier reads a program's peak resident memory — what it actually used
(`generator/src/oracle.cpp`). The judge passes `--mem` to isolate without
`--cg` (`judge/src/supervisor.cpp`), and isolate's own manual says what that
does: *"Limit address space of the program"*. With control groups enabled the
tool reports `cg-mem` instead, *"the total memory use by the whole control
group"*.

So a reference could be certified against its real use and then be refused a
student for the address space it reserved — the same program, two verdicts, and
nothing in the platform saying which one a task's limit means.

## 2. The decision

A task declares two memory limits and both are enforced:

- **used** — the memory the program actually touches. Exceeding it is a
  failure even if the address space is small.
- **reserved** — the address space the program may map. Exceeding it is a
  failure before a byte is touched.

The certifier measures both against the reference and records both halves; the
judge enforces both on a submission. A task that declares only the used limit
is refused by the content gate, because the operator's choice was two limits
and a default would be the platform deciding for them.

The operator chose this over measuring one number, because each catches a
different mistake: a program that maps a gigabyte and touches a megabyte is
wasting the machine's address space, and a program that stays inside its
address space while touching every page is using memory it should not.

## 3. Consequences

- A task's limits are two numbers, and both appear in the statement so a
  student knows what is counted.
- The judge needs two enforcement points: an `RLIMIT_AS` for the reservation,
  and a reading of the process's peak resident memory for the use. Neither
  needs a container-level control group, so the deployment does not grow a
  privilege.
- The certifier's half-limit rule (0048) applies to both numbers.
- A refusal names which of the two was exceeded, because "memory limit
  exceeded" without that word tells a student nothing they can act on.

## 4. What this rejects

- **Reserved only.** It is what the judge does today and the cheapest to
  enforce, and it fails programs that reserve more than they touch, which is a
  mistake the student did not make.
- **Used only.** Simple and it matches what most contest judges report, but a
  program can hold an arbitrary amount of the machine's address space while
  touching almost none of it.

## 5. How it landed

The two numbers are named `memory_used_mb` and `memory_reserved_mb` in a task file's `limits`, and
`memory_used_limit_mb` / `memory_reserved_limit_mb` in `tasks` and `generation_specifications`; the
used limit may not exceed the reserved one, in the schema and in the content gate.

The generator derives both from the same estimate it already made: the values the bound needs are the
used limit, and the reserved limit is twice that with a 64 MB floor, because a program maps more than it
touches. The certifier measures the reference's peak resident size (`VmHWM`) and peak address space
(`VmPeak`) on every generated case, and applies 0048's half rule to each number separately; its
certificate sentence records both measurements and both halves.

The judge passes the reserved limit to `isolate --mem` (address space, no `--cg`), and after a clean
run compares the `max-rss` in isolate's report against the used limit. A refusal says which one:
"used memory: the program touched … MB, above the … MB used limit" or "reserved memory: the program
mapped more than the … MB reserved limit". A clean report with no readable `max-rss` is a refusal, not
a pass: a limit nobody measured cannot be enforced.
