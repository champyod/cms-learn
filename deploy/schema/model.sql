-- A model is a record an operator adds and picks (ADR 0055). The worker reads the effective record — the enabled
-- default — and calls it through the adapter its protocol names, so a second model or a new protocol shape is a row and
-- a code path, never a redeploy of configuration.
CREATE TYPE model_protocol AS ENUM ('openai_chat');

CREATE TABLE models (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  name text NOT NULL UNIQUE,
  base_url text NOT NULL,
  protocol model_protocol NOT NULL,
  model_id text NOT NULL,
  -- The key is sealed before it reaches this column (ADR 0054 §4): sealing is what the api does with the deployment's
  -- key, and the CHECK below refuses anything that is not in the shape core produces.
  sealed_key text NOT NULL,
  enabled boolean NOT NULL DEFAULT true,
  is_default boolean NOT NULL DEFAULT false,
  created_at timestamptz NOT NULL DEFAULT now(),
  updated_at timestamptz NOT NULL DEFAULT now(),
  actor_kind account_kind NOT NULL,
  learner_actor_id uuid REFERENCES learner_accounts (id),
  admin_actor_id uuid REFERENCES admin_accounts (id),
  system_actor_id uuid REFERENCES system_accounts (id),
  CONSTRAINT models_name_shape CHECK (name ~ '^[a-z][a-z0-9_-]{0,63}$'),
  CONSTRAINT models_base_url_shape CHECK (base_url ~ '^https?://[^[:space:]]+$'),
  CONSTRAINT models_model_id_shape CHECK (model_id <> ''),
  CONSTRAINT models_key_sealed CHECK (sealed_key LIKE 'sealed:v1:%'),
  CONSTRAINT models_one_actor CHECK (
    (actor_kind = 'learner' AND learner_actor_id IS NOT NULL
      AND admin_actor_id IS NULL AND system_actor_id IS NULL)
    OR (actor_kind = 'admin' AND admin_actor_id IS NOT NULL
      AND learner_actor_id IS NULL AND system_actor_id IS NULL)
    OR (actor_kind = 'system' AND system_actor_id IS NOT NULL
      AND learner_actor_id IS NULL AND admin_actor_id IS NULL)
  )
);

-- The actor columns and the timestamp come from the session the write runs in, the way every other audited table gets
-- them: a writer names who it is, and the trigger is what puts that into the row (settings_stamp is the same shape).
CREATE FUNCTION models_stamp() RETURNS trigger
LANGUAGE plpgsql
SET search_path = pg_catalog, public
AS $$
DECLARE
  actor actor_identity := current_actor();
BEGIN
  NEW.updated_at := now();
  NEW.actor_kind := actor.kind;
  NEW.learner_actor_id := CASE WHEN actor.kind = 'learner' THEN actor.id END;
  NEW.admin_actor_id := CASE WHEN actor.kind = 'admin' THEN actor.id END;
  NEW.system_actor_id := CASE WHEN actor.kind = 'system' THEN actor.id END;
  RETURN NEW;
END;
$$;

-- At most one model is the default. A partial unique index is the record's own rule rather than a convention the page
-- keeps: two defaults would make "the effective model" answer differently to two readers of the same database.
CREATE UNIQUE INDEX models_one_default ON models (is_default) WHERE is_default;

CREATE TABLE models_audit (
  id bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
  model_id uuid NOT NULL,
  name text NOT NULL,
  old_enabled boolean,
  new_enabled boolean,
  old_is_default boolean,
  new_is_default boolean,
  old_sealed_key text,
  new_sealed_key text,
  actor_kind account_kind NOT NULL,
  actor_id uuid,
  at timestamptz NOT NULL DEFAULT now(),
  CONSTRAINT models_audit_key_sealed CHECK (
    (old_sealed_key IS NULL OR old_sealed_key LIKE 'sealed:v1:%')
    AND (new_sealed_key IS NULL OR new_sealed_key LIKE 'sealed:v1:%'))
);

-- The audit row records that the key changed, never the key: it stores the sealed string, which only the deployment's
-- key opens, so a reader of the audit trail cannot read a credential out of it.
CREATE FUNCTION models_record_audit() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
  IF TG_OP = 'DELETE' THEN
    INSERT INTO models_audit (model_id, name, old_enabled, new_enabled, old_is_default, new_is_default,
                              old_sealed_key, new_sealed_key, actor_kind, actor_id)
    VALUES (OLD.id, OLD.name, OLD.enabled, NULL, OLD.is_default, NULL, OLD.sealed_key, NULL,
            OLD.actor_kind, COALESCE(OLD.admin_actor_id, OLD.system_actor_id, OLD.learner_actor_id));
    RETURN OLD;
  END IF;
  INSERT INTO models_audit (model_id, name, old_enabled, new_enabled, old_is_default, new_is_default,
                            old_sealed_key, new_sealed_key, actor_kind, actor_id)
  VALUES (NEW.id, NEW.name,
          CASE WHEN TG_OP = 'UPDATE' THEN OLD.enabled END, NEW.enabled,
          CASE WHEN TG_OP = 'UPDATE' THEN OLD.is_default END, NEW.is_default,
          CASE WHEN TG_OP = 'UPDATE' THEN OLD.sealed_key END, NEW.sealed_key,
          NEW.actor_kind, COALESCE(NEW.admin_actor_id, NEW.system_actor_id, NEW.learner_actor_id));
  RETURN NEW;
END;
$$;

CREATE TRIGGER models_audit_after_write
  AFTER INSERT OR UPDATE OR DELETE ON models
  FOR EACH ROW EXECUTE FUNCTION models_record_audit();

CREATE TRIGGER models_stamp_before_write
  BEFORE INSERT OR UPDATE ON models
  FOR EACH ROW EXECUTE FUNCTION models_stamp();
