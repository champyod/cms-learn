CREATE TYPE audit_operation AS ENUM (
    'insert',
    'update',
    'delete'
);

CREATE TYPE capability AS ENUM (
    'content:read',
    'content:write',
    'content:publish',
    'submission:read',
    'submission:grade',
    'user:read',
    'user:write',
    'user:approve',
    'settings:read',
    'settings:write',
    'infra:read',
    'infra:write',
    'audit:read',
    'schema:apply'
);

CREATE TYPE locale AS ENUM (
    'en',
    'th'
);

CREATE TYPE principal_kind AS ENUM (
    'user',
    'system'
);

CREATE TYPE principal_status AS ENUM (
    'pending',
    'active',
    'suspended',
    'closed'
);

CREATE TYPE progress_status AS ENUM (
    'attempted',
    'solved'
);

CREATE TYPE setting_kind AS ENUM (
    'bool',
    'int',
    'text'
);

CREATE TYPE setting_tier AS ENUM (
    'public',
    'operator',
    'secret'
);

CREATE TYPE task_checker_kind AS ENUM (
    'standard',
    'token',
    'testlib',
    'interactive'
);

CREATE TYPE task_origin_kind AS ENUM (
    'original',
    'imported',
    'ai'
);

CREATE TYPE task_status AS ENUM (
    'draft',
    'verified',
    'published',
    'rejected'
);

CREATE TYPE task_verification_status AS ENUM (
    'pending',
    'verified',
    'failed'
);

CREATE TYPE token_purpose AS ENUM (
    'verify_email',
    'reset_password'
);

ALTER DEFAULT PRIVILEGES FOR ROLE postgres IN SCHEMA public GRANT SELECT, USAGE ON SEQUENCES TO cmslearn_app;

CREATE TABLE IF NOT EXISTS content_seed (
    singleton boolean DEFAULT true,
    version bigint DEFAULT 0 NOT NULL,
    content_hash text DEFAULT '' NOT NULL,
    seeded_at timestamptz,
    CONSTRAINT content_seed_pkey PRIMARY KEY (singleton),
    CONSTRAINT content_seed_single_row CHECK (singleton)
);

CREATE TABLE IF NOT EXISTS principals (
    id uuid DEFAULT gen_random_uuid(),
    kind principal_kind NOT NULL,
    handle text NOT NULL,
    email text,
    email_verified_at timestamptz,
    password_hash text,
    status principal_status NOT NULL,
    created_at timestamptz DEFAULT now() NOT NULL,
    CONSTRAINT principals_pkey PRIMARY KEY (id),
    CONSTRAINT principals_email_key UNIQUE (email),
    CONSTRAINT principals_handle_key UNIQUE (handle),
    CONSTRAINT principals_id_kind_key UNIQUE (id, kind),
    CONSTRAINT principals_handle_shape CHECK (handle ~ '^[a-z0-9][a-z0-9_.-]{2,63}$'::text OR kind = 'system'::principal_kind AND handle ~ '^system:[a-z][a-z0-9_-]{1,62}$'::text),
    CONSTRAINT principals_system_has_no_credential CHECK (kind <> 'system'::principal_kind OR password_hash IS NULL AND email IS NULL)
);

CREATE TABLE IF NOT EXISTS audit_events (
    id bigint GENERATED ALWAYS AS IDENTITY,
    occurred_at timestamptz DEFAULT now() NOT NULL,
    actor_id uuid NOT NULL,
    invoked_by text NOT NULL,
    action text NOT NULL,
    subject text DEFAULT '' NOT NULL,
    detail jsonb DEFAULT '{}' NOT NULL,
    client_address inet,
    CONSTRAINT audit_events_pkey PRIMARY KEY (id),
    CONSTRAINT audit_events_actor_id_fkey FOREIGN KEY (actor_id) REFERENCES principals (id),
    CONSTRAINT audit_events_action_check CHECK (action ~ '^[a-z][a-z0-9_]*(\.[a-z][a-z0-9_]*)+$'::text),
    CONSTRAINT audit_events_invoked_by_check CHECK (length(invoked_by) > 0)
);

CREATE INDEX IF NOT EXISTS audit_events_actor_idx ON audit_events (actor_id, occurred_at DESC);

CREATE TABLE IF NOT EXISTS elevations (
    id uuid DEFAULT gen_random_uuid(),
    principal_id uuid NOT NULL,
    capability capability NOT NULL,
    granted_by uuid NOT NULL,
    approved_by uuid,
    granted_at timestamptz DEFAULT now() NOT NULL,
    expires_at timestamptz NOT NULL,
    consumed_at timestamptz,
    reason text NOT NULL,
    CONSTRAINT elevations_pkey PRIMARY KEY (id),
    CONSTRAINT elevations_approved_by_fkey FOREIGN KEY (approved_by) REFERENCES principals (id),
    CONSTRAINT elevations_granted_by_fkey FOREIGN KEY (granted_by) REFERENCES principals (id),
    CONSTRAINT elevations_principal_id_fkey FOREIGN KEY (principal_id) REFERENCES principals (id) ON DELETE CASCADE,
    CONSTRAINT elevations_expiry_after_grant CHECK (expires_at > granted_at),
    CONSTRAINT elevations_grantor_not_self CHECK (granted_by <> principal_id),
    CONSTRAINT elevations_maximum_minutes CHECK (expires_at <= (granted_at + '01:00:00'::interval)),
    CONSTRAINT elevations_reason_present CHECK (length(btrim(reason)) > 0),
    CONSTRAINT elevations_two_people CHECK (approved_by IS NULL OR approved_by <> principal_id AND approved_by <> granted_by)
);

CREATE INDEX IF NOT EXISTS elevations_live_idx ON elevations (principal_id, capability, expires_at) WHERE (consumed_at IS NULL);

CREATE TABLE IF NOT EXISTS principal_tokens (
    id uuid DEFAULT gen_random_uuid(),
    principal_id uuid NOT NULL,
    purpose token_purpose NOT NULL,
    token_hash text NOT NULL,
    created_at timestamptz DEFAULT now() NOT NULL,
    expires_at timestamptz NOT NULL,
    used_at timestamptz,
    CONSTRAINT principal_tokens_pkey PRIMARY KEY (id),
    CONSTRAINT principal_tokens_principal_id_fkey FOREIGN KEY (principal_id) REFERENCES principals (id) ON DELETE CASCADE,
    CONSTRAINT principal_tokens_expiry_after_creation CHECK (expires_at > created_at),
    CONSTRAINT principal_tokens_hash_shape CHECK (token_hash ~ '^[0-9a-f]{64}$'::text)
);

CREATE UNIQUE INDEX IF NOT EXISTS principal_tokens_hash_idx ON principal_tokens (token_hash);

CREATE INDEX IF NOT EXISTS principal_tokens_live_idx ON principal_tokens (principal_id) WHERE (used_at IS NULL);

CREATE TABLE IF NOT EXISTS roles (
    id uuid DEFAULT gen_random_uuid(),
    name text NOT NULL,
    description text DEFAULT '' NOT NULL,
    created_at timestamptz DEFAULT now() NOT NULL,
    CONSTRAINT roles_pkey PRIMARY KEY (id),
    CONSTRAINT roles_name_key UNIQUE (name),
    CONSTRAINT roles_name_check CHECK (name ~ '^[a-z][a-z0-9_]{1,62}$'::text)
);

CREATE TABLE IF NOT EXISTS principal_roles (
    principal_id uuid,
    role_id uuid,
    granted_by uuid,
    granted_at timestamptz DEFAULT now() NOT NULL,
    CONSTRAINT principal_roles_pkey PRIMARY KEY (principal_id, role_id),
    CONSTRAINT principal_roles_granted_by_fkey FOREIGN KEY (granted_by) REFERENCES principals (id),
    CONSTRAINT principal_roles_principal_id_fkey FOREIGN KEY (principal_id) REFERENCES principals (id) ON DELETE CASCADE,
    CONSTRAINT principal_roles_role_id_fkey FOREIGN KEY (role_id) REFERENCES roles (id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS role_capabilities (
    role_id uuid,
    capability capability,
    CONSTRAINT role_capabilities_pkey PRIMARY KEY (role_id, capability),
    CONSTRAINT role_capabilities_role_id_fkey FOREIGN KEY (role_id) REFERENCES roles (id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS sessions (
    id uuid DEFAULT gen_random_uuid(),
    principal_id uuid NOT NULL,
    principal_kind principal_kind DEFAULT 'user'::principal_kind NOT NULL,
    created_at timestamptz DEFAULT now() NOT NULL,
    last_seen_at timestamptz DEFAULT now() NOT NULL,
    expires_at timestamptz NOT NULL,
    revoked_at timestamptz,
    user_agent text,
    client_address inet,
    CONSTRAINT sessions_pkey PRIMARY KEY (id),
    CONSTRAINT sessions_principal_fk FOREIGN KEY (principal_id, principal_kind) REFERENCES principals (id, kind) ON DELETE CASCADE,
    CONSTRAINT sessions_expiry_after_creation CHECK (expires_at > created_at),
    CONSTRAINT sessions_user_principal_only CHECK (principal_kind = 'user'::principal_kind)
);

CREATE INDEX IF NOT EXISTS sessions_live_idx ON sessions (principal_id, expires_at) WHERE (revoked_at IS NULL);

CREATE TABLE IF NOT EXISTS settings (
    key text,
    kind setting_kind NOT NULL,
    value_text text NOT NULL,
    tier setting_tier DEFAULT 'operator'::setting_tier NOT NULL,
    description text DEFAULT '' NOT NULL,
    updated_at timestamptz DEFAULT now() NOT NULL,
    updated_by uuid NOT NULL,
    CONSTRAINT settings_pkey PRIMARY KEY (key),
    CONSTRAINT settings_updated_by_fkey FOREIGN KEY (updated_by) REFERENCES principals (id),
    CONSTRAINT settings_key_shape CHECK (key ~ '^[a-z][a-z0-9_]*(\.[a-z][a-z0-9_]*){1,7}$'::text),
    CONSTRAINT settings_value_matches_kind CHECK (CASE kind
    WHEN 'bool'::setting_kind THEN value_text IN ('true'::text, 'false'::text)
    WHEN 'int'::setting_kind THEN
    CASE
        WHEN value_text ~ '^-?(0|[1-9][0-9]{0,18})$'::text THEN value_text::numeric >= '-9223372036854775808'::bigint::numeric AND value_text::numeric <= '9223372036854775807'::bigint::numeric
        ELSE false
    END
    WHEN 'text'::setting_kind THEN true
    ELSE NULL::boolean
END)
);

ALTER TABLE settings ENABLE ROW LEVEL SECURITY;

CREATE POLICY settings_app_all ON settings TO cmslearn_app USING (true) WITH CHECK (true);

CREATE POLICY settings_judge_read ON settings FOR SELECT TO cmslearn_judge USING (tier <> 'secret'::setting_tier);

CREATE TABLE IF NOT EXISTS settings_audit (
    id bigint GENERATED ALWAYS AS IDENTITY,
    occurred_at timestamptz DEFAULT now() NOT NULL,
    key text NOT NULL,
    operation audit_operation NOT NULL,
    old_kind setting_kind,
    new_kind setting_kind,
    old_value text,
    new_value text,
    old_tier setting_tier,
    new_tier setting_tier,
    actor_id uuid NOT NULL,
    invoked_by text DEFAULT '' NOT NULL,
    CONSTRAINT settings_audit_pkey PRIMARY KEY (id),
    CONSTRAINT settings_audit_actor_id_fkey FOREIGN KEY (actor_id) REFERENCES principals (id),
    CONSTRAINT settings_audit_update_has_old_state CHECK (operation <> 'update'::audit_operation OR old_kind IS NOT NULL AND new_kind IS NOT NULL)
);

CREATE INDEX IF NOT EXISTS settings_audit_key_idx ON settings_audit (key, occurred_at DESC);

CREATE TABLE IF NOT EXISTS settings_meta (
    singleton boolean DEFAULT true,
    version bigint DEFAULT 1 NOT NULL,
    updated_at timestamptz DEFAULT now() NOT NULL,
    CONSTRAINT settings_meta_pkey PRIMARY KEY (singleton),
    CONSTRAINT settings_meta_single_row CHECK (singleton)
);

CREATE TABLE IF NOT EXISTS settings_plans (
    id uuid DEFAULT gen_random_uuid(),
    author_id uuid NOT NULL,
    changes jsonb NOT NULL,
    created_at timestamptz DEFAULT now() NOT NULL,
    expires_at timestamptz DEFAULT (now() + '00:15:00'::interval) NOT NULL,
    applied_at timestamptz,
    CONSTRAINT settings_plans_pkey PRIMARY KEY (id),
    CONSTRAINT settings_plans_author_id_fkey FOREIGN KEY (author_id) REFERENCES principals (id),
    CONSTRAINT settings_plans_expiry_after_creation CHECK (expires_at > created_at)
);

CREATE TABLE IF NOT EXISTS tasks (
    id text,
    version integer,
    status task_status NOT NULL,
    title_en text NOT NULL,
    title_th text NOT NULL,
    difficulty integer NOT NULL,
    time_limit_ms integer NOT NULL,
    memory_limit_mb integer NOT NULL,
    checker task_checker_kind NOT NULL,
    checker_path text DEFAULT '' NOT NULL,
    origin task_origin_kind NOT NULL,
    verification task_verification_status NOT NULL,
    solution text NOT NULL,
    content_hash text NOT NULL,
    seeded boolean DEFAULT true NOT NULL,
    seeded_at timestamptz DEFAULT now() NOT NULL,
    CONSTRAINT tasks_pkey PRIMARY KEY (id, version),
    CONSTRAINT tasks_checker_path CHECK ((checker = 'testlib'::task_checker_kind) = (length(checker_path) > 0)),
    CONSTRAINT tasks_difficulty_band CHECK (difficulty >= 800 AND difficulty <= 3500 AND (difficulty % 100) = 0),
    CONSTRAINT tasks_memory_limit CHECK (memory_limit_mb >= 16 AND memory_limit_mb <= 1024),
    CONSTRAINT tasks_time_limit CHECK (time_limit_ms >= 100 AND time_limit_ms <= 20000),
    CONSTRAINT tasks_title_en_check CHECK (length(btrim(title_en)) > 0),
    CONSTRAINT tasks_title_th_check CHECK (length(btrim(title_th)) > 0)
);

CREATE INDEX IF NOT EXISTS tasks_published_idx ON tasks (difficulty, id) WHERE (status = 'published'::task_status);

CREATE TABLE IF NOT EXISTS progress (
    id bigint GENERATED ALWAYS AS IDENTITY,
    account_id uuid NOT NULL,
    task_id text NOT NULL,
    task_version integer NOT NULL,
    status progress_status DEFAULT 'attempted'::progress_status NOT NULL,
    attempted_at timestamptz DEFAULT now() NOT NULL,
    solved_at timestamptz,
    CONSTRAINT progress_pkey PRIMARY KEY (id),
    CONSTRAINT progress_account_id_fkey FOREIGN KEY (account_id) REFERENCES principals (id) ON DELETE CASCADE,
    CONSTRAINT progress_task_fkey FOREIGN KEY (task_id, task_version) REFERENCES tasks (id, version) ON DELETE CASCADE,
    CONSTRAINT progress_solved_has_time CHECK ((status = 'solved'::progress_status) = (solved_at IS NOT NULL))
);

CREATE UNIQUE INDEX IF NOT EXISTS progress_account_task_idx ON progress (account_id, task_id, task_version);

CREATE TABLE IF NOT EXISTS task_examples (
    task_id text,
    task_version integer,
    ordinal integer,
    input text NOT NULL,
    expected_output text NOT NULL,
    CONSTRAINT task_examples_pkey PRIMARY KEY (task_id, task_version, ordinal),
    CONSTRAINT task_examples_task_fkey FOREIGN KEY (task_id, task_version) REFERENCES tasks (id, version) ON DELETE CASCADE,
    CONSTRAINT task_examples_ordinal_check CHECK (ordinal >= 1)
);

CREATE TABLE IF NOT EXISTS task_statements (
    task_id text,
    task_version integer,
    locale locale,
    body text NOT NULL,
    CONSTRAINT task_statements_pkey PRIMARY KEY (task_id, task_version, locale),
    CONSTRAINT task_statements_task_fkey FOREIGN KEY (task_id, task_version) REFERENCES tasks (id, version) ON DELETE CASCADE,
    CONSTRAINT task_statements_body_check CHECK (length(btrim(body)) > 0)
);

CREATE TABLE IF NOT EXISTS topics (
    id text,
    label_en text NOT NULL,
    label_th text NOT NULL,
    CONSTRAINT topics_pkey PRIMARY KEY (id),
    CONSTRAINT topics_id_shape CHECK (id ~ '^[a-z0-9][a-z0-9-]{1,63}$'::text),
    CONSTRAINT topics_label_en_check CHECK (length(btrim(label_en)) > 0),
    CONSTRAINT topics_label_th_check CHECK (length(btrim(label_th)) > 0)
);

CREATE TABLE IF NOT EXISTS task_topics (
    task_id text,
    task_version integer,
    topic text,
    CONSTRAINT task_topics_pkey PRIMARY KEY (task_id, task_version, topic),
    CONSTRAINT task_topics_task_fkey FOREIGN KEY (task_id, task_version) REFERENCES tasks (id, version) ON DELETE CASCADE,
    CONSTRAINT task_topics_topic_fkey FOREIGN KEY (topic) REFERENCES topics (id)
);

CREATE INDEX IF NOT EXISTS task_topics_topic_idx ON task_topics (topic);

CREATE TABLE IF NOT EXISTS tracks (
    id text,
    position integer NOT NULL,
    title_en text NOT NULL,
    title_th text NOT NULL,
    description_en text NOT NULL,
    description_th text NOT NULL,
    content_hash text NOT NULL,
    seeded boolean DEFAULT true NOT NULL,
    CONSTRAINT tracks_pkey PRIMARY KEY (id),
    CONSTRAINT tracks_position_unique UNIQUE (position),
    CONSTRAINT tracks_description_en_check CHECK (length(btrim(description_en)) > 0),
    CONSTRAINT tracks_description_th_check CHECK (length(btrim(description_th)) > 0),
    CONSTRAINT tracks_id_shape CHECK (id ~ '^t-[a-z0-9][a-z0-9-]{0,62}$'::text),
    CONSTRAINT tracks_title_en_check CHECK (length(btrim(title_en)) > 0),
    CONSTRAINT tracks_title_th_check CHECK (length(btrim(title_th)) > 0)
);

CREATE TABLE IF NOT EXISTS track_stages (
    track_id text,
    id text,
    position integer NOT NULL,
    title_en text NOT NULL,
    title_th text NOT NULL,
    difficulty_min integer NOT NULL,
    difficulty_max integer NOT NULL,
    CONSTRAINT track_stages_pkey PRIMARY KEY (track_id, id),
    CONSTRAINT track_stages_position_unique UNIQUE (track_id, position),
    CONSTRAINT track_stages_track_fkey FOREIGN KEY (track_id) REFERENCES tracks (id) ON DELETE CASCADE,
    CONSTRAINT track_stages_band CHECK (difficulty_min <= difficulty_max),
    CONSTRAINT track_stages_difficulty_band CHECK (difficulty_min >= 800 AND difficulty_min <= 3500 AND difficulty_max >= 800 AND difficulty_max <= 3500 AND (difficulty_min % 100) = 0 AND (difficulty_max % 100) = 0),
    CONSTRAINT track_stages_id_shape CHECK (id ~ '^st-[a-z0-9][a-z0-9-]{0,62}$'::text),
    CONSTRAINT track_stages_title_en_check CHECK (length(btrim(title_en)) > 0),
    CONSTRAINT track_stages_title_th_check CHECK (length(btrim(title_th)) > 0)
);

CREATE TABLE IF NOT EXISTS stage_tags (
    track_id text,
    stage_id text,
    tag text,
    CONSTRAINT stage_tags_pkey PRIMARY KEY (track_id, stage_id, tag),
    CONSTRAINT stage_tags_stage_fkey FOREIGN KEY (track_id, stage_id) REFERENCES track_stages (track_id, id) ON DELETE CASCADE,
    CONSTRAINT stage_tags_tag_fkey FOREIGN KEY (tag) REFERENCES topics (id)
);

CREATE INDEX IF NOT EXISTS stage_tags_tag_idx ON stage_tags (tag);

CREATE OR REPLACE FUNCTION current_actor()
RETURNS uuid
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

CREATE OR REPLACE FUNCTION reject_mutation()
RETURNS trigger
LANGUAGE plpgsql
VOLATILE
SET search_path = pg_catalog, public
AS $$
BEGIN
  RAISE EXCEPTION '% is append-only; % is not permitted', TG_TABLE_NAME, TG_OP
    USING ERRCODE = 'restrict_violation';
END;
$$;

CREATE OR REPLACE FUNCTION reject_published_task_change()
RETURNS trigger
LANGUAGE plpgsql
VOLATILE
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

CREATE OR REPLACE FUNCTION settings_bump_version()
RETURNS trigger
LANGUAGE plpgsql
VOLATILE
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

CREATE OR REPLACE FUNCTION settings_stamp()
RETURNS trigger
LANGUAGE plpgsql
VOLATILE
SET search_path = pg_catalog, public
AS $$
BEGIN
  NEW.updated_at := now();
  NEW.updated_by := current_actor();
  RETURN NEW;
END;
$$;

CREATE OR REPLACE FUNCTION settings_write_audit()
RETURNS trigger
LANGUAGE plpgsql
VOLATILE
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

CREATE OR REPLACE TRIGGER audit_events_append_only
    BEFORE UPDATE OR DELETE ON audit_events
    FOR EACH ROW
    EXECUTE FUNCTION reject_mutation();

CREATE OR REPLACE TRIGGER settings_audit_append_only
    BEFORE UPDATE OR DELETE ON settings_audit
    FOR EACH ROW
    EXECUTE FUNCTION reject_mutation();

CREATE OR REPLACE TRIGGER settings_audit_row
    AFTER INSERT OR UPDATE OR DELETE ON settings
    FOR EACH ROW
    EXECUTE FUNCTION settings_write_audit();

CREATE OR REPLACE TRIGGER settings_stamp_row
    BEFORE INSERT OR UPDATE ON settings
    FOR EACH ROW
    EXECUTE FUNCTION settings_stamp();

CREATE OR REPLACE TRIGGER settings_version_bump
    AFTER INSERT OR UPDATE OR DELETE ON settings
    FOR EACH ROW
    EXECUTE FUNCTION settings_bump_version();

CREATE OR REPLACE TRIGGER tasks_published_immutable
    BEFORE UPDATE ON tasks
    FOR EACH ROW
    EXECUTE FUNCTION reject_published_task_change();

REVOKE EXECUTE ON FUNCTION current_actor() FROM PUBLIC;

REVOKE EXECUTE ON FUNCTION settings_bump_version() FROM PUBLIC;

REVOKE EXECUTE ON FUNCTION settings_write_audit() FROM PUBLIC;

GRANT EXECUTE ON FUNCTION current_actor() TO cmslearn_app;

GRANT SELECT, USAGE ON SEQUENCE audit_events_id_seq TO cmslearn_app;

GRANT SELECT, USAGE ON SEQUENCE progress_id_seq TO cmslearn_app;

GRANT SELECT, USAGE ON SEQUENCE settings_audit_id_seq TO cmslearn_app;

GRANT INSERT, SELECT ON TABLE audit_events TO cmslearn_app;

GRANT INSERT, SELECT, UPDATE ON TABLE content_seed TO cmslearn_app;

GRANT INSERT, SELECT, UPDATE ON TABLE elevations TO cmslearn_app;

GRANT DELETE, INSERT, SELECT ON TABLE principal_roles TO cmslearn_app;

GRANT INSERT, SELECT, UPDATE ON TABLE principal_tokens TO cmslearn_app;

GRANT INSERT, SELECT, UPDATE ON TABLE principals TO cmslearn_app;

GRANT DELETE, INSERT, SELECT, UPDATE ON TABLE progress TO cmslearn_app;

GRANT DELETE, INSERT, SELECT ON TABLE role_capabilities TO cmslearn_app;

GRANT INSERT, SELECT, UPDATE ON TABLE roles TO cmslearn_app;

GRANT INSERT, SELECT, UPDATE ON TABLE sessions TO cmslearn_app;

GRANT DELETE, INSERT, SELECT, UPDATE ON TABLE settings TO cmslearn_app;

GRANT SELECT ON TABLE settings TO cmslearn_judge;

GRANT SELECT ON TABLE settings_audit TO cmslearn_app;

GRANT SELECT ON TABLE settings_meta TO cmslearn_app;

GRANT SELECT ON TABLE settings_meta TO cmslearn_judge;

GRANT INSERT, SELECT, UPDATE ON TABLE settings_plans TO cmslearn_app;

GRANT DELETE, INSERT, SELECT, UPDATE ON TABLE stage_tags TO cmslearn_app;

GRANT DELETE, INSERT, SELECT, UPDATE ON TABLE task_examples TO cmslearn_app;

GRANT SELECT ON TABLE task_examples TO cmslearn_judge;

GRANT DELETE, INSERT, SELECT, UPDATE ON TABLE task_statements TO cmslearn_app;

GRANT DELETE, INSERT, SELECT, UPDATE ON TABLE task_topics TO cmslearn_app;

GRANT DELETE, INSERT, SELECT, UPDATE ON TABLE tasks TO cmslearn_app;

GRANT SELECT ON TABLE tasks TO cmslearn_judge;

GRANT DELETE, INSERT, SELECT, UPDATE ON TABLE topics TO cmslearn_app;

GRANT DELETE, INSERT, SELECT, UPDATE ON TABLE track_stages TO cmslearn_app;

GRANT DELETE, INSERT, SELECT, UPDATE ON TABLE tracks TO cmslearn_app;
