-- What an account submitted, and what the judge said about it. One row per attempt: a learner may submit a
-- task many times, and the history is the point.

CREATE TABLE submissions (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  account_id uuid NOT NULL REFERENCES principals (id) ON DELETE CASCADE,
  task_id text NOT NULL,
  task_version integer NOT NULL,
  source text NOT NULL CHECK (length(source) > 0),
  submitted_at timestamptz NOT NULL DEFAULT now(),
  verdict verdict_status,
  verdict_detail text,
  judged_at timestamptz,
  CONSTRAINT submissions_task_fkey FOREIGN KEY (task_id, task_version)
    REFERENCES tasks (id, version) ON DELETE CASCADE,
  -- A verdict and the moment it was reached travel together, or neither exists.
  CONSTRAINT submissions_verdict_has_time CHECK ((verdict IS NULL) = (judged_at IS NULL))
);

CREATE INDEX submissions_account_idx ON submissions (account_id, submitted_at DESC);
CREATE INDEX submissions_unjudged_idx ON submissions (submitted_at) WHERE verdict IS NULL;
