-- The task bank. Content is data: an author edits files under content/, the seeder imports them once by
-- content hash, and every surface reads the rows rather than the files.

CREATE TABLE topics (
  id text PRIMARY KEY,
  label_en text NOT NULL CHECK (length(btrim(label_en)) > 0),
  label_th text NOT NULL CHECK (length(btrim(label_th)) > 0),
  CONSTRAINT topics_id_shape CHECK (id ~ '^[a-z0-9][a-z0-9-]{1,63}$')
);

CREATE TABLE tasks (
  id text NOT NULL,
  version integer NOT NULL,
  status task_status NOT NULL,
  title_en text NOT NULL CHECK (length(btrim(title_en)) > 0),
  title_th text NOT NULL CHECK (length(btrim(title_th)) > 0),
  difficulty integer NOT NULL,
  time_limit_ms integer NOT NULL,
  memory_limit_mb integer NOT NULL,
  checker task_checker_kind NOT NULL,
  checker_path text NOT NULL DEFAULT '',
  origin task_origin_kind NOT NULL,
  verification task_verification_status NOT NULL,
  solution text NOT NULL,
  -- The hash of every file this row was built from. A re-seed compares it and writes nothing when it
  -- matches, which is what makes the import idempotent rather than merely repeatable.
  content_hash text NOT NULL,
  -- False means an administrator owns this row; the seeder imports but never overwrites one.
  seeded boolean NOT NULL DEFAULT true,
  seeded_at timestamptz NOT NULL DEFAULT now(),
  CONSTRAINT tasks_pkey PRIMARY KEY (id, version),
  CONSTRAINT tasks_difficulty_band CHECK (
    difficulty BETWEEN 800 AND 3500 AND difficulty % 100 = 0
  ),
  CONSTRAINT tasks_time_limit CHECK (time_limit_ms BETWEEN 100 AND 20000),
  CONSTRAINT tasks_memory_limit CHECK (memory_limit_mb BETWEEN 16 AND 1024),
  -- A checker that names a file has one, and one that does not carries none. Written as a
  -- biconditional so the two states cannot drift apart, with the empty string standing for "none"
  -- rather than a nullable column the writer has to remember to null.
  CONSTRAINT tasks_checker_path CHECK ((checker = 'testlib') = (length(checker_path) > 0))
);

CREATE INDEX tasks_published_idx ON tasks (difficulty, id)
  WHERE status = 'published';

CREATE TABLE task_topics (
  task_id text NOT NULL,
  task_version integer NOT NULL,
  topic text NOT NULL REFERENCES topics (id),
  CONSTRAINT task_topics_pkey PRIMARY KEY (task_id, task_version, topic),
  CONSTRAINT task_topics_task_fkey FOREIGN KEY (task_id, task_version)
    REFERENCES tasks (id, version) ON DELETE CASCADE
);

CREATE INDEX task_topics_topic_idx ON task_topics (topic);

CREATE TABLE task_statements (
  task_id text NOT NULL,
  task_version integer NOT NULL,
  locale locale NOT NULL,
  body text NOT NULL CHECK (length(btrim(body)) > 0),
  CONSTRAINT task_statements_pkey PRIMARY KEY (task_id, task_version, locale),
  CONSTRAINT task_statements_task_fkey FOREIGN KEY (task_id, task_version)
    REFERENCES tasks (id, version) ON DELETE CASCADE
);

CREATE TABLE task_examples (
  task_id text NOT NULL,
  task_version integer NOT NULL,
  ordinal integer NOT NULL CHECK (ordinal >= 1),
  input text NOT NULL,
  expected_output text NOT NULL,
  CONSTRAINT task_examples_pkey PRIMARY KEY (task_id, task_version, ordinal),
  CONSTRAINT task_examples_task_fkey FOREIGN KEY (task_id, task_version)
    REFERENCES tasks (id, version) ON DELETE CASCADE
);

CREATE TABLE content_seed (
  singleton boolean PRIMARY KEY DEFAULT true,
  version bigint NOT NULL DEFAULT 0,
  content_hash text NOT NULL DEFAULT '',
  seeded_at timestamptz,
  CONSTRAINT content_seed_single_row CHECK (singleton)
);

-- A published row is what a student is served, so it is immutable in the only way that matters: the
-- content hash cannot change under the same (id, version). A correction is a new version.
CREATE FUNCTION reject_published_task_change() RETURNS trigger
LANGUAGE plpgsql
SET search_path = pg_catalog, public
AS $$
BEGIN
  IF OLD.status = 'published' AND NEW.content_hash <> OLD.content_hash THEN
    RAISE EXCEPTION 'task % version % is published; publish a new version instead',
      OLD.id, OLD.version
      USING ERRCODE = 'restrict_violation';
  END IF;
  RETURN NEW;
END;
$$;

CREATE TRIGGER tasks_published_immutable
  BEFORE UPDATE ON tasks
  FOR EACH ROW EXECUTE FUNCTION reject_published_task_change();
