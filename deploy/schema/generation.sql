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
  created_at timestamptz NOT NULL DEFAULT now(),
  updated_at timestamptz NOT NULL DEFAULT now(),
  CONSTRAINT generation_jobs_task_fkey FOREIGN KEY (task_id, task_version)
    REFERENCES tasks (id, version) ON DELETE CASCADE,
  CONSTRAINT generation_jobs_claim CHECK ((claimed_by IS NULL) = (claimed_at IS NULL))
);

CREATE INDEX generation_jobs_queue_idx ON generation_jobs (created_at)
  WHERE state = 'queued';

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
