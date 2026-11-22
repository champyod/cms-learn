-- What an account submitted, and what the judge said about it. One row per attempt: a learner may submit a
-- task many times, and the history is the point.

CREATE TABLE submissions (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  account_id uuid NOT NULL REFERENCES learner_accounts (id) ON DELETE CASCADE,
  task_id text NOT NULL,
  task_version integer NOT NULL,
  source text NOT NULL CHECK (length(source) > 0),
  submitted_at timestamptz NOT NULL DEFAULT now(),
  verdict verdict_status,
  verdict_detail text,
  judged_at timestamptz,
  -- Which worker took this row and when. The pair is the lease: the moment is what a second worker compares
  -- against, the name is what a human reads after a worker dies without clearing it.
  claimed_by text,
  claimed_at timestamptz,
  CONSTRAINT submissions_task_fkey FOREIGN KEY (task_id, task_version)
    REFERENCES tasks (id, version) ON DELETE CASCADE,
  -- A verdict and the moment it was reached travel together, or neither exists.
  CONSTRAINT submissions_verdict_has_time CHECK ((verdict IS NULL) = (judged_at IS NULL)),
  -- Half a claim cannot be told apart from no claim, so the range of states is exactly two.
  CONSTRAINT submissions_claim_complete CHECK ((claimed_by IS NULL) = (claimed_at IS NULL))
);

CREATE INDEX submissions_account_idx ON submissions (account_id, submitted_at DESC);
CREATE INDEX submissions_unjudged_idx ON submissions (submitted_at) WHERE verdict IS NULL;

-- The verdict is written by the judge and read by another process; the write itself carries the news so an
-- open page is fed by the change rather than by asking again. The payload names the row's audience: the
-- account, the task and the version a stream compares against.
CREATE FUNCTION submissions_verdict_notifies() RETURNS trigger
LANGUAGE plpgsql
SET search_path = pg_catalog, public
AS $$
BEGIN
  PERFORM pg_notify('cms_verdict',
                    NEW.account_id::text || ':' || NEW.task_id || ':' || NEW.task_version::text);
  RETURN NEW;
END;
$$;

CREATE TRIGGER submissions_verdict_notify
  AFTER UPDATE OF verdict ON submissions
  FOR EACH ROW
  WHEN (OLD.verdict IS DISTINCT FROM NEW.verdict)
  EXECUTE FUNCTION submissions_verdict_notifies();

