-- The generation pipeline's record (ADR 0015, ADR 0028). Seven steps run in a worker with its own role; this is
-- where their work is recorded and where "evidence only" is enforced: the worker writes what a step produced
-- and from which call, and its role cannot touch the two columns that decide publication.

CREATE TYPE generation_state AS ENUM ('queued', 'running', 'failed', 'awaiting-evidence', 'done');

CREATE TABLE generation_jobs (
  id bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
  task_id text NOT NULL,
  task_version integer NOT NULL,
  -- What the teacher asked for, written when the job is created and before any model call (ADR 0036 §2). An empty
  -- request is refused by name when the leak checks run, so a job created before the form existed is not compared
  -- against nothing; the step's writer never reads these two columns, which is the absence the leak rule promises.
  brief text NOT NULL DEFAULT '' CHECK (brief = btrim(brief)),
  description text NOT NULL DEFAULT '' CHECK (description = btrim(description)),
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
  ),
  -- One job per task version. A second job for the same version would repeat every model call and write a record
  -- nobody can tell from the first, so a re-run is a new task version — which is what the version is for.
  CONSTRAINT generation_jobs_task_version_unique UNIQUE (task_id, task_version)
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
  position integer NOT NULL CHECK (position > 0),
  approach text NOT NULL CHECK (length(btrim(approach)) > 0),
  intended_complexity text NOT NULL,
  fits_bounds boolean NOT NULL,
  plan jsonb NOT NULL,
  chosen boolean NOT NULL DEFAULT false,
  -- Every row of a decision says why it is there: the chosen one why it won, the others why they lost. A blank
  -- reason is a decision that explains nothing, and the record is the evidence.
  reason text NOT NULL CHECK (length(btrim(reason)) > 0),
  -- The call that produced this plan (ADR 0030). Required, so a candidate cannot be recorded without the request
  -- that made it; the reference is added after the artefact table, which is defined below because it holds them all.
  call_artefact_id bigint NOT NULL,
  UNIQUE (job_id, position)
);

-- A decision is a set, and one row cannot see the set: two rows marked chosen are two answers to one question, and
-- a position above the highest recorded one names a candidate the decision does not have. The check therefore runs
-- at commit, when every row the caller wrote in that transaction is visible, so a set that breaks the rule lands
-- nowhere instead of halfway.
CREATE FUNCTION generation_candidates_are_one_decision() RETURNS trigger
LANGUAGE plpgsql
SET search_path = pg_catalog, public
AS $$
DECLARE
  recorded bigint;
  highest integer;
  chosen_rows bigint;
BEGIN
  SELECT count(*), max(position), count(*) FILTER (WHERE chosen)
    INTO recorded, highest, chosen_rows
    FROM generation_candidates WHERE job_id = NEW.job_id;
  IF recorded = 0 THEN
    RETURN NULL;
  END IF;
  IF chosen_rows <> 1 THEN
    RAISE EXCEPTION 'generation job % records % chosen candidates; a decision chooses exactly one',
      NEW.job_id, chosen_rows
      USING ERRCODE = 'check_violation';
  END IF;
  IF highest <> recorded THEN
    RAISE EXCEPTION 'generation job % records % candidates numbered to %, so position % is not a candidate',
      NEW.job_id, recorded, highest, highest
      USING ERRCODE = 'check_violation';
  END IF;
  RETURN NULL;
END;
$$;

CREATE CONSTRAINT TRIGGER generation_candidates_one_decision
  AFTER INSERT OR UPDATE ON generation_candidates
  DEFERRABLE INITIALLY DEFERRED
  FOR EACH ROW EXECUTE FUNCTION generation_candidates_are_one_decision();

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
  CONSTRAINT generation_artefacts_payload CHECK (object_key IS NOT NULL OR inline_text IS NOT NULL),
  -- One row per model call (ADR 0030): recording the same call again replaces its row rather than adding a second,
  -- while a second call at the same step, kind and locale is a second row. A locale may be absent, and two absent
  -- locales are one locale, which NULLS NOT DISTINCT states.
  CONSTRAINT generation_artefacts_call_unique UNIQUE NULLS NOT DISTINCT (job_id, step, kind, locale, call_index)
);

CREATE INDEX generation_artefacts_job_idx ON generation_artefacts (job_id, step);

-- A candidate names the call that produced it. The reference is its own constraint, declared after both tables, so
-- the order they are created in does not decide whether a candidate can be written without one. It does not cascade:
-- a call row is evidence, and deleting it would silently delete the decision that cites it.
ALTER TABLE generation_candidates
  ADD CONSTRAINT generation_candidates_call_fkey
  FOREIGN KEY (call_artefact_id) REFERENCES generation_artefacts (id) ON DELETE NO ACTION;

-- NO ACTION checks at the end of the statement, so deleting a job still removes its decisions and their calls
-- together while a lone artefact delete is refused. The refusal a foreign key gives names its constraint and
-- nothing else, so this trigger says which decision would lose its provenance instead.
CREATE FUNCTION generation_artefact_is_cited() RETURNS trigger
LANGUAGE plpgsql
SET search_path = pg_catalog, public
AS $$
DECLARE
  cited record;
BEGIN
  -- A job's own delete cascades to its artefacts after the job row is gone; its decisions are going with it, so the
  -- citation is not a reason to refuse.
  IF NOT EXISTS (SELECT 1 FROM generation_jobs j WHERE j.id = OLD.job_id) THEN
    RETURN OLD;
  END IF;
  SELECT c.job_id, c.position INTO cited
    FROM generation_candidates c
   WHERE c.call_artefact_id = OLD.id
   LIMIT 1;
  IF FOUND THEN
    RAISE EXCEPTION 'artefact % is the call that produced position % of generation job %; the decision cannot lose the call that made it',
      OLD.id, cited.position, cited.job_id
      USING ERRCODE = 'restrict_violation';
  END IF;
  RETURN OLD;
END;
$$;

CREATE TRIGGER generation_artefacts_are_cited
  BEFORE DELETE ON generation_artefacts
  FOR EACH ROW EXECUTE FUNCTION generation_artefact_is_cited();

-- Step 3's record: the specification it read the plan into, the one limit pair it derived, and the oracle it wrote.
-- The limits are the task's (ADR 0015, interpretation of 2026-11-17); their band is the band a task's own columns
-- allow, so a derivation that lands outside it is refused here as well as by the step (deploy/schema/content.sql:39).
CREATE TABLE generation_specifications (
  job_id bigint PRIMARY KEY REFERENCES generation_jobs (id) ON DELETE CASCADE,
  specification jsonb NOT NULL,
  time_limit_ms integer NOT NULL CHECK (time_limit_ms BETWEEN 100 AND 20000),
  memory_limit_mb integer NOT NULL CHECK (memory_limit_mb BETWEEN 16 AND 1024),
  -- The sentence a reader needs to see that the limit was computed, not chosen: the complexity, the bound, the
  -- operations, the band, and how the value landed in it.
  derivation text NOT NULL CHECK (length(btrim(derivation)) > 0),
  answer_artefact_id bigint NOT NULL REFERENCES generation_artefacts (id) ON DELETE NO ACTION,
  oracle_artefact_id bigint NOT NULL REFERENCES generation_artefacts (id) ON DELETE NO ACTION,
  created_at timestamptz NOT NULL DEFAULT now(),
  CONSTRAINT generation_specifications_calls_distinct CHECK (answer_artefact_id <> oracle_artefact_id)
);

-- Step 5's record: one row per program it wrote, the reference at position 0 and each subtask's solution at its
-- subtask's position, each citing the artefact that holds it and carrying the proof it earned (ADR 0015, step 5;
-- ADR 0033 keeps a subtask's weight a measurement only while a weaker solution earns it).
CREATE TABLE generation_solutions (
  job_id bigint NOT NULL REFERENCES generation_jobs (id) ON DELETE CASCADE,
  position integer NOT NULL CHECK (position >= 0),
  artefact_id bigint NOT NULL REFERENCES generation_artefacts (id) ON DELETE NO ACTION,
  proof text NOT NULL CHECK (length(btrim(proof)) > 0),
  created_at timestamptz NOT NULL DEFAULT now(),
  PRIMARY KEY (job_id, position)
);

-- Step 6's record: the two programs it wrote and the cases each subtask needs (ADR 0015, step 6). A row cites the
-- artefact that holds the bytes, so a proof cannot point at a program that was never stored.
CREATE TABLE generation_programs (
  job_id bigint NOT NULL REFERENCES generation_jobs (id) ON DELETE CASCADE,
  role text NOT NULL CHECK (role IN ('generator', 'checker')),
  artefact_id bigint NOT NULL REFERENCES generation_artefacts (id) ON DELETE NO ACTION,
  proof text NOT NULL CHECK (length(btrim(proof)) > 0),
  created_at timestamptz NOT NULL DEFAULT now(),
  PRIMARY KEY (job_id, role)
);

CREATE TABLE generation_cases (
  job_id bigint NOT NULL REFERENCES generation_jobs (id) ON DELETE CASCADE,
  subtask_position integer NOT NULL CHECK (subtask_position > 0),
  artefact_id bigint NOT NULL REFERENCES generation_artefacts (id) ON DELETE NO ACTION,
  case_count integer NOT NULL CHECK (case_count > 0),
  proof text NOT NULL CHECK (length(btrim(proof)) > 0),
  created_at timestamptz NOT NULL DEFAULT now(),
  PRIMARY KEY (job_id, subtask_position)
);

-- The worker's role holds the record and reads the brief; it holds no privilege that could publish or verify.
-- Written as its own grants so a reader sees what the role can do and, by omission, what it cannot.
GRANT SELECT, INSERT, UPDATE ON generation_jobs TO cmslearn_generator;
GRANT SELECT, INSERT ON generation_candidates TO cmslearn_generator;
GRANT SELECT, INSERT ON generation_artefacts TO cmslearn_generator;
GRANT SELECT, INSERT ON generation_specifications TO cmslearn_generator;
GRANT SELECT, INSERT ON generation_solutions TO cmslearn_generator;
GRANT SELECT, INSERT ON generation_programs TO cmslearn_generator;
GRANT SELECT, INSERT ON generation_cases TO cmslearn_generator;
GRANT SELECT (id, version, task_type, status, title_en, title_th, difficulty, time_limit_ms,
              memory_limit_mb, checker, checker_path, origin, verification, content_hash, seeded)
  ON tasks TO cmslearn_generator;
GRANT SELECT ON task_types, task_field_defs, task_field_values, task_topics, topics TO cmslearn_generator;

-- A certificate is what a later reader reads to know what a past verification meant: what the certifier checked, the
-- commit it checked, and the content addresses it saw. Only the certifier role writes one, and a verification change
-- without one is refused by the trigger below, so "verified" is reachable only through step 7.
CREATE TABLE generation_certificates (
  id bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
  job_id bigint NOT NULL REFERENCES generation_jobs (id) ON DELETE CASCADE,
  task_id text NOT NULL,
  task_version integer NOT NULL,
  outcome task_verification_status NOT NULL,
  checks jsonb NOT NULL CHECK (jsonb_typeof(checks) = 'array' AND jsonb_array_length(checks) > 0),
  commit_id text NOT NULL CHECK (length(btrim(commit_id)) > 0),
  evidence jsonb NOT NULL CHECK (jsonb_typeof(evidence) = 'object'),
  created_at timestamptz NOT NULL DEFAULT now(),
  CONSTRAINT generation_certificates_task_fkey FOREIGN KEY (task_id, task_version)
    REFERENCES tasks (id, version) ON DELETE CASCADE
);

CREATE INDEX generation_certificates_task_idx
  ON generation_certificates (task_id, task_version, created_at DESC);

CREATE FUNCTION task_verification_needs_its_certificate() RETURNS trigger
LANGUAGE plpgsql
SET search_path = pg_catalog, public
AS $$
BEGIN
  IF TG_OP = 'UPDATE' AND NEW.verification IS NOT DISTINCT FROM OLD.verification THEN
    RETURN NEW;
  END IF;
  -- A new row may start unverified, which is the state the seeder writes. Nothing else is created without the
  -- certificate that says it, and the certificate's foreign key makes that impossible to fake: a certificate names a
  -- task row, so none can exist for a row that does not exist yet.
  IF TG_OP = 'INSERT' AND NEW.verification = 'pending' THEN
    RETURN NEW;
  END IF;
  IF NOT EXISTS (SELECT 1 FROM generation_certificates c
                  WHERE c.task_id = NEW.id AND c.task_version = NEW.version
                    AND c.outcome = NEW.verification) THEN
    RAISE EXCEPTION 'task % version % has no certificate for %, so its verification cannot be written: it is written by cmslearn_certifier, which holds INSERT on generation_certificates and UPDATE (verification) on tasks',
      NEW.id, NEW.version, NEW.verification
      USING ERRCODE = 'check_violation';
  END IF;
  RETURN NEW;
END;
$$;

CREATE TRIGGER tasks_verification_needs_certificate
  BEFORE UPDATE OF verification ON tasks
  FOR EACH ROW EXECUTE FUNCTION task_verification_needs_its_certificate();

-- The same rule for the insert path, so the guarantee is a property of the table and not of the statement a caller
-- happens to use (ADR 0046 §2).
CREATE TRIGGER tasks_verification_needs_certificate_on_insert
  BEFORE INSERT ON tasks
  FOR EACH ROW EXECUTE FUNCTION task_verification_needs_its_certificate();

CREATE FUNCTION job_done_needs_its_certificate() RETURNS trigger
LANGUAGE plpgsql
SET search_path = pg_catalog, public
AS $$
BEGIN
  IF NEW.state IS NOT DISTINCT FROM OLD.state OR NEW.state <> 'done' THEN
    RETURN NEW;
  END IF;
  -- The same rule the task side has, on the job's own move: a job is done only when a verified certificate for its task
  -- and version exists. The pipeline calls it in that order, but the table is where every future caller must pass.
  IF NOT EXISTS (SELECT 1 FROM generation_certificates c
                  WHERE c.job_id = NEW.id AND c.task_id = NEW.task_id AND c.task_version = NEW.task_version
                    AND c.outcome = 'verified') THEN
    RAISE EXCEPTION 'job % for task % version % has no verified certificate, so it cannot move to done: the certificate is written by cmslearn_certifier, which holds INSERT on generation_certificates',
      NEW.id, NEW.task_id, NEW.task_version
      USING ERRCODE = 'check_violation';
  END IF;
  RETURN NEW;
END;
$$;

CREATE TRIGGER generation_jobs_done_needs_certificate
  BEFORE UPDATE OF state ON generation_jobs
  FOR EACH ROW EXECUTE FUNCTION job_done_needs_its_certificate();

-- The certifier is not the generator: it writes the certificate and moves the one column a verification is. It holds
-- no privilege on a task's content, status or anything the generator writes, and the generator holds none of these.
GRANT SELECT, INSERT ON generation_certificates TO cmslearn_certifier;
GRANT UPDATE (verification) ON tasks TO cmslearn_certifier;
GRANT SELECT (id, version, status, verification, content_hash, time_limit_ms, memory_limit_mb, points)
  ON tasks TO cmslearn_certifier;
GRANT SELECT ON generation_jobs, generation_artefacts, generation_specifications, generation_solutions,
                generation_programs, generation_cases, task_subtasks
  TO cmslearn_certifier;
