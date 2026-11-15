-- The generation pipeline's record (ADR 0015, ADR 0028). Seven steps run in a worker with its own role; this is
-- where their work is recorded and where "evidence only" is enforced: the worker writes what a step produced
-- and from which call, and its role cannot touch the two columns that decide publication.

CREATE TYPE generation_state AS ENUM ('queued', 'running', 'failed', 'awaiting-evidence', 'done');

CREATE TABLE generation_jobs (
  id bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
  task_id text NOT NULL,
  task_version integer NOT NULL,
  state generation_state NOT NULL DEFAULT 'queued',
  attempts integer NOT NULL DEFAULT 0,
  claimed_by text,
  claimed_at timestamptz,
  -- When the claim runs out. A worker that dies holds the row only until this moment: the next claim takes a
  -- running job whose lease expired, which is what keeps one dead worker from parking a job forever.
  lease_expires_at timestamptz,
  created_at timestamptz NOT NULL DEFAULT now(),
  updated_at timestamptz NOT NULL DEFAULT now(),
  CONSTRAINT generation_jobs_task_fkey FOREIGN KEY (task_id, task_version)
    REFERENCES tasks (id, version) ON DELETE CASCADE,
  -- The claim is all three facts or none: a name with no moment cannot be compared, and a deadline with no
  -- holder belongs to nobody.
  CONSTRAINT generation_jobs_claim CHECK (
    (claimed_by IS NULL) = (claimed_at IS NULL) AND
    (claimed_by IS NULL) = (lease_expires_at IS NULL)
  )
);

CREATE INDEX generation_jobs_queue_idx ON generation_jobs (created_at)
  WHERE state = 'queued';

-- The other half of the queue: a running job whose lease ran out is claimable again, and the claim finds it
-- through this index instead of reading every running row.
CREATE INDEX generation_jobs_lease_idx ON generation_jobs (lease_expires_at)
  WHERE state = 'running';

-- A job moves the way the record allows and no other way: queued starts, running ends in evidence, a verdict or
-- a failure, and evidence ends in a verdict or a failure. A finished or failed job is terminal, and a terminal
-- row cannot be given a new holder: a second worker taking it would be work done twice that nobody asked for.
CREATE FUNCTION generation_job_moves_forward() RETURNS trigger
LANGUAGE plpgsql
SET search_path = pg_catalog, public
AS $$
BEGIN
  IF OLD.state IN ('done', 'failed') THEN
    IF NEW.state <> OLD.state OR NEW.claimed_by IS DISTINCT FROM OLD.claimed_by THEN
      RAISE EXCEPTION 'generation job % is %; it is finished and nothing reopens it', OLD.id, OLD.state
        USING ERRCODE = 'restrict_violation';
    END IF;
    RETURN NEW;
  END IF;
  IF NEW.state = OLD.state THEN
    RETURN NEW;
  END IF;
  IF (OLD.state = 'queued' AND NEW.state IN ('running', 'failed')) OR
     (OLD.state = 'running' AND NEW.state IN ('awaiting-evidence', 'done', 'failed')) OR
     (OLD.state = 'awaiting-evidence' AND NEW.state IN ('done', 'failed')) THEN
    RETURN NEW;
  END IF;
  RAISE EXCEPTION 'generation job % cannot move from % to %', OLD.id, OLD.state, NEW.state
    USING ERRCODE = 'check_violation';
END;
$$;

CREATE TRIGGER generation_jobs_move_forward
  BEFORE UPDATE ON generation_jobs
  FOR EACH ROW EXECUTE FUNCTION generation_job_moves_forward();

CREATE TABLE generation_candidates (
  id bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
  job_id bigint NOT NULL REFERENCES generation_jobs (id) ON DELETE CASCADE,
  position integer NOT NULL,
  approach text NOT NULL CHECK (length(btrim(approach)) > 0),
  intended_complexity text NOT NULL,
  fits_bounds boolean NOT NULL,
  plan jsonb NOT NULL,
  chosen boolean NOT NULL DEFAULT false,
  reason text NOT NULL DEFAULT '',
  UNIQUE (job_id, position)
);

CREATE TABLE generation_artefacts (
  id bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
  job_id bigint NOT NULL REFERENCES generation_jobs (id) ON DELETE CASCADE,
  step smallint NOT NULL CHECK (step BETWEEN 1 AND 7),
  kind text NOT NULL CHECK (length(btrim(kind)) > 0),
  locale text,
  object_key text,
  inline_text text,
  -- The digest of the payload's own bytes, required: an artefact is read by its content address, and a row
  -- that cannot name one would make the read a guess (ADR 0023 §2, ADR 0027 §2).
  digest text NOT NULL CHECK (digest ~ '^[0-9a-f]{64}$'),
  model_id text NOT NULL,
  prompt_version text NOT NULL,
  seed bigint NOT NULL,
  call_index integer NOT NULL,
  created_at timestamptz NOT NULL DEFAULT now(),
  CONSTRAINT generation_artefacts_payload CHECK (object_key IS NOT NULL OR inline_text IS NOT NULL)
);

CREATE INDEX generation_artefacts_job_idx ON generation_artefacts (job_id, step);

-- The worker's role holds the record and reads the brief; it holds no privilege that could publish or verify.
-- Written as its own grants so a reader sees what the role can do and, by omission, what it cannot.
GRANT SELECT, INSERT, UPDATE ON generation_jobs TO cmslearn_generator;
GRANT SELECT, INSERT ON generation_candidates TO cmslearn_generator;
GRANT SELECT, INSERT ON generation_artefacts TO cmslearn_generator;
GRANT SELECT (id, version, task_type, status, title_en, title_th, difficulty, time_limit_ms,
              memory_limit_mb, checker, checker_path, origin, verification, content_hash, seeded)
  ON tasks TO cmslearn_generator;
GRANT SELECT ON task_types, task_field_defs, task_field_values, task_topics, topics TO cmslearn_generator;
