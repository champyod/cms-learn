-- A task type and its fields are data, not classes (ADR 0024). Adding a field or a type is a row, so an
-- administrator can extend a task without a rebuild; the checks the compiler used to give for free become
-- constraints here, because a wrong definition is a data mistake and must not reach a reader.

CREATE TYPE task_field_kind AS ENUM ('text', 'integer', 'boolean', 'choice', 'collection', 'file');

CREATE TABLE task_types (
  id text PRIMARY KEY,
  title_en text NOT NULL CHECK (length(btrim(title_en)) > 0),
  title_th text NOT NULL DEFAULT '',
  position integer NOT NULL,
  created_at timestamptz NOT NULL DEFAULT now(),
  CONSTRAINT task_types_id_shape CHECK (id ~ '^[a-z][a-z0-9_]{1,62}$'),
  CONSTRAINT task_types_position_unique UNIQUE (position)
);

CREATE TABLE task_field_defs (
  task_type_id text NOT NULL REFERENCES task_types (id) ON DELETE CASCADE,
  key text NOT NULL,
  kind task_field_kind NOT NULL,
  label_en text NOT NULL CHECK (length(btrim(label_en)) > 0),
  label_th text NOT NULL DEFAULT '',
  required boolean NOT NULL DEFAULT false,
  choices jsonb,
  members jsonb,
  minimum integer,
  maximum integer,
  position integer NOT NULL,
  PRIMARY KEY (task_type_id, key),
  CONSTRAINT task_field_defs_key_shape CHECK (key ~ '^[a-z][a-z0-9_]{1,62}$'),
  CONSTRAINT task_field_defs_position_unique UNIQUE (task_type_id, position),
  -- A choice without choices cannot be rendered and cannot be checked, so the row is refused.
  CONSTRAINT task_field_defs_choices CHECK ((kind = 'choice') = (choices IS NOT NULL)),
  CONSTRAINT task_field_defs_choices_shape CHECK (
    choices IS NULL OR (jsonb_typeof(choices) = 'array' AND jsonb_array_length(choices) >= 2)),
  CONSTRAINT task_field_defs_members CHECK (kind <> 'collection' OR members IS NOT NULL),
  CONSTRAINT task_field_defs_members_shape CHECK (
    members IS NULL OR (jsonb_typeof(members) = 'array' AND jsonb_array_length(members) >= 1)),
  CONSTRAINT task_field_defs_bounds CHECK ((minimum IS NOT NULL OR maximum IS NOT NULL) = (kind = 'integer')),
  CONSTRAINT task_field_defs_bounds_order CHECK (minimum IS NULL OR maximum IS NULL OR minimum <= maximum)
);

-- A collection names fields of its own type, so the members are checked against the rows that exist rather
-- than against a list a reader has to keep in step.
CREATE FUNCTION task_field_members_exist() RETURNS trigger
LANGUAGE plpgsql
SET search_path = pg_catalog, public
AS $$
DECLARE
  missing text;
BEGIN
  IF NEW.kind <> 'collection' THEN
    RETURN NEW;
  END IF;
  SELECT string_agg(member, ', ' ORDER BY member) INTO missing
    FROM jsonb_array_elements_text(NEW.members) AS member
   WHERE member = NEW.key
      OR NOT EXISTS (SELECT 1 FROM task_field_defs d
                      WHERE d.task_type_id = NEW.task_type_id AND d.key = member);
  IF missing IS NOT NULL THEN
    RAISE EXCEPTION 'collection field % names fields that do not exist in %: %',
      NEW.key, NEW.task_type_id, missing
      USING ERRCODE = 'foreign_key_violation';
  END IF;
  RETURN NEW;
END;
$$;

CREATE TRIGGER task_field_defs_members_exist
  BEFORE INSERT OR UPDATE ON task_field_defs
  FOR EACH ROW EXECUTE FUNCTION task_field_members_exist();

CREATE TABLE task_field_values (
  task_id text NOT NULL,
  task_version integer NOT NULL,
  field_key text NOT NULL,
  value jsonb NOT NULL,
  PRIMARY KEY (task_id, task_version, field_key),
  CONSTRAINT task_field_values_task_fkey FOREIGN KEY (task_id, task_version)
    REFERENCES tasks (id, version) ON DELETE CASCADE
);

-- The field a value belongs to is decided by the task's type, so a required field cannot arrive empty: the
-- check reads the definition rows, which is the same source the form renders from.
CREATE FUNCTION task_field_value_is_filled() RETURNS trigger
LANGUAGE plpgsql
SET search_path = pg_catalog, public
AS $$
DECLARE
  definition task_field_defs;
BEGIN
  SELECT d.* INTO definition
    FROM tasks t
    JOIN task_field_defs d ON d.task_type_id = t.task_type AND d.key = NEW.field_key
   WHERE t.id = NEW.task_id AND t.version = NEW.task_version;
  IF NOT FOUND THEN
    RAISE EXCEPTION 'no field % is defined for task % version %',
      NEW.field_key, NEW.task_id, NEW.task_version
      USING ERRCODE = 'foreign_key_violation';
  END IF;
  IF definition.required AND (NEW.value = 'null'::jsonb OR NEW.value = '""'::jsonb
      OR NEW.value = '[]'::jsonb OR NEW.value = '{}'::jsonb) THEN
    RAISE EXCEPTION 'field % is required and cannot hold an empty value', NEW.field_key
      USING ERRCODE = 'check_violation';
  END IF;
  RETURN NEW;
END;
$$;

CREATE TRIGGER task_field_values_filled
  BEFORE INSERT OR UPDATE ON task_field_values
  FOR EACH ROW EXECUTE FUNCTION task_field_value_is_filled();

-- A file-kind field names an artefact, never a store key: ADR 0027 §5 says so, and the panel resolves this id
-- through generation_artefacts to the object's key and digest. A text value is the old reading, and accepting it
-- here would leave one column with two meanings and a reader that has to guess which it holds.
CREATE FUNCTION task_field_value_is_artefact_id() RETURNS trigger
LANGUAGE plpgsql
SET search_path = pg_catalog, public
AS $$
DECLARE
  definition_kind text;
  artefact_id bigint;
BEGIN
  SELECT d.kind INTO definition_kind
    FROM tasks t
    JOIN task_field_defs d ON d.task_type_id = t.task_type AND d.key = NEW.field_key
   WHERE t.id = NEW.task_id AND t.version = NEW.task_version;
  IF definition_kind IS DISTINCT FROM 'file' THEN
    RETURN NEW;
  END IF;
  IF jsonb_typeof(NEW.value) <> 'number' OR (NEW.value #>> '{}') !~ '^[0-9]{1,18}$' THEN
    RAISE EXCEPTION 'the field % holds %, which is a store key or other text, not an artefact id',
      NEW.field_key, NEW.value
      USING ERRCODE = 'check_violation';
  END IF;
  artefact_id := (NEW.value #>> '{}')::bigint;
  IF NOT EXISTS (SELECT 1 FROM generation_artefacts a
                  WHERE a.id = artefact_id AND a.object_key IS NOT NULL) THEN
    RAISE EXCEPTION 'the field % names artefact %, which is not a stored object',
      NEW.field_key, artefact_id
      USING ERRCODE = 'foreign_key_violation';
  END IF;
  RETURN NEW;
END;
$$;

CREATE TRIGGER task_field_values_artefact_id
  BEFORE INSERT OR UPDATE ON task_field_values
  FOR EACH ROW EXECUTE FUNCTION task_field_value_is_artefact_id();

-- Removing a definition whose values exist would orphan them: the row stays, the value becomes unreadable,
-- and nothing says which of the two is wrong.
CREATE FUNCTION task_field_defs_refuse_orphaning() RETURNS trigger
LANGUAGE plpgsql
SET search_path = pg_catalog, public
AS $$
DECLARE
  held bigint;
BEGIN
  SELECT count(*) INTO held
    FROM task_field_values v
    JOIN tasks t ON t.id = v.task_id AND t.version = v.task_version
   WHERE v.field_key = OLD.key AND t.task_type = OLD.task_type_id;
  IF held > 0 THEN
    RAISE EXCEPTION 'field % of % holds % value(s); remove the values before the definition',
      OLD.key, OLD.task_type_id, held
      USING ERRCODE = 'restrict_violation';
  END IF;
  RETURN OLD;
END;
$$;

CREATE TRIGGER task_field_defs_refuse_orphaning
  BEFORE DELETE ON task_field_defs
  FOR EACH ROW EXECUTE FUNCTION task_field_defs_refuse_orphaning();

-- A definition change changes what every task of that type means, so it is an audit event naming the actor,
-- on the same shape the settings tables use.
CREATE FUNCTION task_field_audit() RETURNS trigger
LANGUAGE plpgsql
SECURITY DEFINER
SET search_path = pg_catalog, public
AS $$
DECLARE
  actor actor_identity := current_actor();
  subject text;
BEGIN
  subject := CASE WHEN TG_OP = 'DELETE' THEN OLD.task_type_id || '.' || OLD.key
                  ELSE NEW.task_type_id || '.' || NEW.key END;
  INSERT INTO audit_events (actor_kind, learner_actor_id, admin_actor_id, system_actor_id,
                            invoked_by, action, subject, detail)
  VALUES (actor.kind,
          CASE WHEN actor.kind = 'learner' THEN actor.id END,
          CASE WHEN actor.kind = 'admin' THEN actor.id END,
          CASE WHEN actor.kind = 'system' THEN actor.id END,
          coalesce(current_setting('cms.invoked_by', true), ''),
          'task.field.' || lower(TG_OP),
          subject,
          jsonb_build_object('kind', CASE WHEN TG_OP = 'DELETE' THEN OLD.kind ELSE NEW.kind END,
                             'required', CASE WHEN TG_OP = 'DELETE' THEN OLD.required ELSE NEW.required END));
  RETURN NULL;
END;
$$;

CREATE TRIGGER task_field_defs_audit
  AFTER INSERT OR UPDATE OR DELETE ON task_field_defs
  FOR EACH ROW EXECUTE FUNCTION task_field_audit();

CREATE FUNCTION task_type_audit() RETURNS trigger
LANGUAGE plpgsql
SECURITY DEFINER
SET search_path = pg_catalog, public
AS $$
DECLARE
  actor actor_identity := current_actor();
BEGIN
  INSERT INTO audit_events (actor_kind, learner_actor_id, admin_actor_id, system_actor_id,
                            invoked_by, action, subject, detail)
  VALUES (actor.kind,
          CASE WHEN actor.kind = 'learner' THEN actor.id END,
          CASE WHEN actor.kind = 'admin' THEN actor.id END,
          CASE WHEN actor.kind = 'system' THEN actor.id END,
          coalesce(current_setting('cms.invoked_by', true), ''),
          'task.type.' || lower(TG_OP),
          CASE WHEN TG_OP = 'DELETE' THEN OLD.id ELSE NEW.id END,
          jsonb_build_object('position', CASE WHEN TG_OP = 'DELETE' THEN OLD.position ELSE NEW.position END));
  RETURN NULL;
END;
$$;

CREATE TRIGGER task_types_audit
  AFTER INSERT OR UPDATE OR DELETE ON task_types
  FOR EACH ROW EXECUTE FUNCTION task_type_audit();
