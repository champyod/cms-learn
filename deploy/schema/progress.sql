-- What an account has touched. "Unseen" is a join against this table, so the index that makes that join
-- cheap is the object the plan's falsification drops.

CREATE TYPE progress_status AS ENUM ('attempted', 'solved');

CREATE TABLE progress (
  id bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
  account_id uuid NOT NULL REFERENCES principals (id) ON DELETE CASCADE,
  task_id text NOT NULL,
  task_version integer NOT NULL,
  status progress_status NOT NULL DEFAULT 'attempted',
  attempted_at timestamptz NOT NULL DEFAULT now(),
  solved_at timestamptz,
  CONSTRAINT progress_task_fkey FOREIGN KEY (task_id, task_version)
    REFERENCES tasks (id, version) ON DELETE CASCADE,
  CONSTRAINT progress_solved_has_time CHECK ((status = 'solved') = (solved_at IS NOT NULL))
);

-- One object serving two purposes on purpose: it is the rule that an account has one row per task
-- version, and it is the access path the unseen-task query reads. Splitting it into a unique constraint
-- plus a separate index would let the falsification drop one while the planner kept using the other,
-- which would prove nothing.
CREATE UNIQUE INDEX progress_account_task_idx ON progress (account_id, task_id, task_version);
