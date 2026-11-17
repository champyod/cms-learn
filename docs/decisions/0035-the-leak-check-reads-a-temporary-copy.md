# 0035 — The leak check reads the brief as a temporary copy

Status: accepted 2026-11-17 · decision by the operator

## 1. The question

Two checks compare a statement with the teacher's brief: `check:brief-leak` refuses a statement that
repeats eight words of the brief, the description or the plan, and `check:algo-leak` refuses one that
names the intended approach while misdirection is enabled (`docs/task-generation.md`).

The brief is not stored beside the statement (`docs/decisions/0015` §3). It survives in the generation
audit row. So the check needs the brief handed to it, and this record fixes how.

## 2. The decision

| Step | What happens |
|---|---|
| the statement step writes the brief, the description and the plan | to a temporary directory outside `content/` |
| the check runs | over those paths and the statement files, as one command |
| the directory is removed | after the comparison, on the refusal path as well as the passing one |
| what is recorded | nothing about the temporary path; a task row never names it |

The check therefore takes file paths, like every other gate in this repository, and its inputs are
gone by the time the run ends.

## 3. Why not read the audit row

The brief lives in the database, so a check that reads it needs a scratch cluster. `check:live` pays
that cost for cases that assert database behaviour, and it runs in the full tier. A check that can only
run there is not a check a change passes — and this one guards what a student reads.

## 4. Why not certify inside the statement step

A producer that certifies its own output is the defect class this repository has closed eleven times:
*a proof that supplies its own conditions*. The step would compare its answer, in memory, using the
rule its author chose, and no second reader would ever see the comparison happen. The check must be a
separate program with its own falsification case, so the refusal can be shown to die when its rule is
removed.

## 5. What still holds

- The brief is never stored beside the statement, is never rendered, and is never committed.
- The temporary copy lives outside `content/` and `docs/`, is removed in the same run, and holds the
  brief only for the length of one comparison.
- The statement step refuses before it writes a statement file, so a leaked statement has no file to
  publish.

## 6. Consequences

- `check:leak` is a shell gate over paths: the fast tier can run it, and the falsification plants
  files the way every other tools case does (`tools/falsify/cases/`).
- The statement step owns one more obligation — leaving no temporary directory behind on any exit
  path, including a refusal and a crash. The verification proves it gone after both.
- `check:hint-leak` and `check:cast`, still unwritten, take the same shape when their turn comes.
