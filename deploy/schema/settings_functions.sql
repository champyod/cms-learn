CREATE FUNCTION current_actor() RETURNS uuid
LANGUAGE plpgsql
STABLE
SET search_path = pg_catalog, public
AS $$
DECLARE
  raw text := current_setting('cms.actor_id', true);
BEGIN
  IF raw IS NULL OR raw = '' THEN
    RAISE EXCEPTION 'cms.actor_id is not set; every settings write must name its actor'
      USING ERRCODE = 'insufficient_privilege';
  END IF;
  RETURN raw::uuid;
END;
$$;

CREATE FUNCTION settings_stamp() RETURNS trigger
LANGUAGE plpgsql
SET search_path = pg_catalog, public
AS $$
BEGIN
  NEW.updated_at := now();
  NEW.updated_by := current_actor();
  RETURN NEW;
END;
$$;

CREATE FUNCTION settings_write_audit() RETURNS trigger
LANGUAGE plpgsql
SECURITY DEFINER
SET search_path = pg_catalog, public
AS $$
DECLARE
  changed_key text;
  old_kind setting_kind;
  new_kind setting_kind;
  old_value text;
  new_value text;
  old_tier setting_tier;
  new_tier setting_tier;
BEGIN
  -- Branches, not COALESCE: OLD is unassigned on INSERT and NEW on DELETE.
  IF TG_OP = 'INSERT' THEN
    changed_key := NEW.key;
    new_kind := NEW.kind;
    new_value := NEW.value_text;
    new_tier := NEW.tier;
  ELSIF TG_OP = 'UPDATE' THEN
    changed_key := NEW.key;
    old_kind := OLD.kind;
    new_kind := NEW.kind;
    old_value := OLD.value_text;
    new_value := NEW.value_text;
    old_tier := OLD.tier;
    new_tier := NEW.tier;
  ELSE
    changed_key := OLD.key;
    old_kind := OLD.kind;
    old_value := OLD.value_text;
    old_tier := OLD.tier;
  END IF;

  IF old_tier = 'secret' THEN
    old_value := '<redacted>';
  END IF;
  IF new_tier = 'secret' THEN
    new_value := '<redacted>';
  END IF;

  INSERT INTO settings_audit (key, operation, old_kind, new_kind, old_value, new_value, old_tier,
                              new_tier, actor_id, invoked_by)
  VALUES (changed_key, lower(TG_OP)::audit_operation, old_kind, new_kind, old_value, new_value,
          old_tier, new_tier, current_actor(),
          coalesce(current_setting('cms.invoked_by', true), ''));
  RETURN NULL;
END;
$$;

CREATE FUNCTION settings_bump_version() RETURNS trigger
LANGUAGE plpgsql
SECURITY DEFINER
SET search_path = pg_catalog, public
AS $$
DECLARE
  published bigint;
BEGIN
  INSERT INTO settings_meta (singleton, version)
  VALUES (true, 1)
  ON CONFLICT (singleton) DO UPDATE
    SET version = settings_meta.version + 1,
        updated_at = now()
  RETURNING version INTO published;

  PERFORM pg_notify('cms_settings', published::text);
  RETURN NULL;
END;
$$;
