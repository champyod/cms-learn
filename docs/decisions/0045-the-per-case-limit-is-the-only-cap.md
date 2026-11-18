# 0045 — The per-case limit is the only cap; there is no per-submission budget

Status: accepted 2026-11-18 · decision by the operator

## 1. The correction this record makes

I framed an open question as "one submission can take 360 seconds", and the operator corrected the
premise: a competitive task's limit is normally **1–2 seconds**, each cap applies **per test case**, and
there is no sum over the cases.

The code agrees with him:

- `judge/src/run.cpp:80-81` — each case runs with `task.time_limit_ms`, and its wall ceiling is
  `task.time_limit_ms * 2 + 5000`.
- `judge/src/supervisor.cpp:142-143` — `isolate` receives `--time` and `--wall-time`, so a spinning
  solution is killed at the **limit**. The wall ceiling is a backstop for a process that ignores the
  signal, not the normal path.

My 180–360 second example used the backstop as if every case reached it. That was wrong, and it is
recorded here rather than quietly dropped.

## 2. The decision

| What | The rule |
|---|---|
| the cap | the task's one limit pair (`0033`), applied to each case |
| a sum over cases | **none** — no per-submission budget is introduced |
| the count | customizable, and the pipeline's choice recorded with its reason (`0044`) |
| the normal range | a 1–2 second limit is the ordinary case; a count inside a competitive task's ordinary range is expected, and an unusual count is a recorded choice rather than a silent one |

## 3. What a slow solution really costs

A solution that exceeds the limit on every case costs about *count × limit*, because `isolate` kills each
case at the limit: twenty cases at two seconds is about **forty seconds**, and a correct solution costs
milliseconds per case. That is the same behaviour every competitive judge has.

## 4. Consequences

- No new setting and no new column: the count (`0044`) remains the only new data.
- `0044` §4's open item — "what bounds the pipeline's count" — is closed by this record: the per-case limit
  bounds each case, and no second cap is added on top.
- If a real task's judging is ever measured as too slow, that measurement comes back as its own record. A
  rule is not invented in advance for a problem nobody has observed.

## 5. What was rejected

- **A per-submission sum budget.** It would put a second cap on the same quantity, and the operator's rule
  is that the limit belongs to the task.
- **A flat maximum number of cases.** It ignores the limit and the shape of the task.
