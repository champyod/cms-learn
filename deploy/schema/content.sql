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
  task_type text,
  status task_status NOT NULL,
  title_en text NOT NULL CHECK (length(btrim(title_en)) > 0),
  title_th text NOT NULL CHECK (length(btrim(title_th)) > 0),
  difficulty integer NOT NULL,
  time_limit_ms integer NOT NULL,
  memory_used_limit_mb integer NOT NULL,
  memory_reserved_limit_mb integer NOT NULL,
  -- What the task is scored out of, stated once on the task: ADR 0033 §2 puts the limits and the total here, never
  -- on a subtask. A task that never names a total is worth 100, the number an olympiad task page states.
  points integer NOT NULL DEFAULT 100 CHECK (points BETWEEN 1 AND 1000),
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
  CONSTRAINT tasks_memory_used_limit CHECK (memory_used_limit_mb BETWEEN 16 AND 1024),
  CONSTRAINT tasks_memory_reserved_limit CHECK (memory_reserved_limit_mb BETWEEN 16 AND 1024),
  CONSTRAINT tasks_memory_limits_ordered CHECK (memory_used_limit_mb <= memory_reserved_limit_mb),
  -- A checker that names a file has one, and one that does not carries none. Written as a
  -- biconditional so the two states cannot drift apart, with the empty string standing for "none"
  -- rather than a nullable column the writer has to remember to null.
  CONSTRAINT tasks_checker_path CHECK ((checker = 'testlib') = (length(checker_path) > 0))
);

CREATE INDEX tasks_published_idx ON tasks (difficulty, id)
  WHERE status = 'published';

-- Publishing is a boundary rule, not a writer's habit (ADR 0046 §2, ADR 0057). A task is created with any status but
-- published, because a certificate names a task row and none can exist before the row does; and a move to published needs
-- a verified certificate for that task and version, the shape the verification trigger already uses.
CREATE FUNCTION task_publish_needs_its_certificate() RETURNS trigger
LANGUAGE plpgsql
SET search_path = pg_catalog, public
AS $$
BEGIN
  IF TG_OP = 'INSERT' AND NEW.status = 'published' THEN
    RAISE EXCEPTION 'task % version % cannot be created published: ship it as draft, certify it, then publish the certified version',
      NEW.id, NEW.version
      USING ERRCODE = 'check_violation';
  END IF;
  IF TG_OP = 'UPDATE' AND NEW.status = 'published' AND NEW.status IS DISTINCT FROM OLD.status THEN
    IF NOT EXISTS (SELECT 1 FROM generation_certificates c
                    WHERE c.task_id = NEW.id AND c.task_version = NEW.version AND c.outcome = 'verified') THEN
      RAISE EXCEPTION 'task % version % has no verified certificate, so it cannot move to published: the certificate is written by cmslearn_certifier',
        NEW.id, NEW.version
        USING ERRCODE = 'check_violation';
    END IF;
  END IF;
  RETURN NEW;
END;
$$;

CREATE TRIGGER tasks_publish_needs_certificate
  BEFORE INSERT OR UPDATE OF status ON tasks
  FOR EACH ROW EXECUTE FUNCTION task_publish_needs_its_certificate();

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

-- A subtask is a group of tests, worth points, defined by the constraint that makes the simpler idea sufficient
-- (ADR 0033 §2). It carries no limits: time and memory are the task's, stated once.
CREATE TABLE task_subtasks (
  task_id text NOT NULL,
  task_version integer NOT NULL,
  position integer NOT NULL CHECK (position > 0),
  points integer NOT NULL CHECK (points > 0 AND points <= 1000),
  -- The constraint that defines the group, as a typed narrowing of the task's own bounds (ADR 0041): an absent column
  -- is the task's value. The sorted order is the group's interval, so a narrowing that admits no case is refused here
  -- rather than by a reader.
  min_n bigint,
  max_n bigint,
  value_min bigint,
  value_max bigint,
  CONSTRAINT task_subtasks_narrows_something CHECK (num_nonnulls(min_n, max_n, value_min, value_max) > 0),
  CONSTRAINT task_subtasks_admits_a_case CHECK (
    (min_n IS NULL OR max_n IS NULL OR min_n <= max_n) AND
    (value_min IS NULL OR value_max IS NULL OR value_min <= value_max)),
  CONSTRAINT task_subtasks_pkey PRIMARY KEY (task_id, task_version, position),
  CONSTRAINT task_subtasks_task_fkey FOREIGN KEY (task_id, task_version)
    REFERENCES tasks (id, version) ON DELETE CASCADE
);

-- A task's subtasks are a partition of its total, and they only exist on a task that scores by subtask. Both are
-- facts about a set of rows rather than about one row, so the check runs at commit, when the whole set a caller
-- wrote is visible: a set that breaks either rule lands nowhere instead of halfway.
CREATE FUNCTION task_subtasks_are_a_partition() RETURNS trigger
LANGUAGE plpgsql
SET search_path = pg_catalog, public
AS $$
DECLARE
  task_key text;
  task_version_here integer;
  total integer;
  recorded bigint;
  awarded bigint;
  mode text;
BEGIN
  IF TG_OP = 'DELETE' THEN
    task_key := OLD.task_id;
    task_version_here := OLD.task_version;
  ELSE
    task_key := NEW.task_id;
    task_version_here := NEW.task_version;
  END IF;

  SELECT t.points INTO total FROM tasks t WHERE t.id = task_key AND t.version = task_version_here;
  IF NOT FOUND THEN
    RETURN NULL;  -- the task is gone and the cascade took its subtasks with it
  END IF;

  SELECT count(*), coalesce(sum(s.points), 0) INTO recorded, awarded
    FROM task_subtasks s WHERE s.task_id = task_key AND s.task_version = task_version_here;

  SELECT v.value #>> '{}' INTO mode
    FROM task_field_values v
   WHERE v.task_id = task_key AND v.task_version = task_version_here AND v.field_key = 'score_mode';

  IF mode IS DISTINCT FROM 'subtasks' THEN
    IF recorded > 0 THEN
      RAISE EXCEPTION 'task % version % scores by %, not by subtask, so it cannot carry subtasks',
        task_key, task_version_here, coalesce(mode, 'no declared score mode')
        USING ERRCODE = 'check_violation';
    END IF;
    RETURN NULL;
  END IF;

  IF awarded <> total THEN
    RAISE EXCEPTION 'task % version % scores by subtask, so its subtasks must add up to %, and they add up to %',
      task_key, task_version_here, total, awarded
      USING ERRCODE = 'check_violation';
  END IF;
  RETURN NULL;
END;
$$;

CREATE CONSTRAINT TRIGGER task_subtasks_partition
  AFTER INSERT OR UPDATE OR DELETE ON task_subtasks
  DEFERRABLE INITIALLY DEFERRED
  FOR EACH ROW EXECUTE FUNCTION task_subtasks_are_a_partition();

-- The total is part of the partition too: moving it while subtasks exist must keep them adding up to it.
CREATE FUNCTION task_points_keep_the_partition() RETURNS trigger
LANGUAGE plpgsql
SET search_path = pg_catalog, public
AS $$
DECLARE
  recorded bigint;
  awarded bigint;
BEGIN
  IF NEW.points = OLD.points THEN
    RETURN NULL;
  END IF;
  SELECT count(*), coalesce(sum(s.points), 0) INTO recorded, awarded
    FROM task_subtasks s WHERE s.task_id = NEW.id AND s.task_version = NEW.version;
  IF recorded > 0 AND awarded <> NEW.points THEN
    RAISE EXCEPTION 'task % version % is worth % now, and its subtasks add up to %; move them together',
      NEW.id, NEW.version, NEW.points, awarded
      USING ERRCODE = 'check_violation';
  END IF;
  RETURN NULL;
END;
$$;

CREATE CONSTRAINT TRIGGER task_subtasks_follow_the_total
  AFTER UPDATE ON tasks
  DEFERRABLE INITIALLY DEFERRED
  FOR EACH ROW EXECUTE FUNCTION task_points_keep_the_partition();

-- A published task is what a learner is served, so a task that scores by subtask cannot reach that state with no
-- subtasks to score against.
CREATE FUNCTION task_published_needs_its_subtasks() RETURNS trigger
LANGUAGE plpgsql
SET search_path = pg_catalog, public
AS $$
DECLARE
  mode text;
  recorded bigint;
BEGIN
  IF NEW.status <> 'published' THEN
    RETURN NEW;
  END IF;
  SELECT v.value #>> '{}' INTO mode
    FROM task_field_values v
   WHERE v.task_id = NEW.id AND v.task_version = NEW.version AND v.field_key = 'score_mode';
  IF mode IS DISTINCT FROM 'subtasks' THEN
    RETURN NEW;
  END IF;
  SELECT count(*) INTO recorded
    FROM task_subtasks s WHERE s.task_id = NEW.id AND s.task_version = NEW.version;
  IF recorded = 0 THEN
    RAISE EXCEPTION 'task % version % scores by subtask and has none; it cannot be published',
      NEW.id, NEW.version
      USING ERRCODE = 'check_violation';
  END IF;
  RETURN NEW;
END;
$$;

CREATE TRIGGER tasks_published_needs_subtasks
  BEFORE INSERT OR UPDATE OF status ON tasks
  FOR EACH ROW EXECUTE FUNCTION task_published_needs_its_subtasks();

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
