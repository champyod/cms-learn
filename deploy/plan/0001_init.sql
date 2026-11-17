CREATE TYPE account_kind AS ENUM (
    'learner',
    'admin',
    'system'
);

CREATE TYPE actor_identity AS (kind account_kind, id uuid);

CREATE TYPE admin_status AS ENUM (
    'active',
    'revoked'
);

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
    'settings:secret_write',
    'infra:read',
    'infra:write',
    'audit:read',
    'schema:apply'
);

CREATE TYPE generation_state AS ENUM (
    'queued',
    'running',
    'failed',
    'awaiting-evidence',
    'done'
);

CREATE TYPE locale AS ENUM (
    'en',
    'th'
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

CREATE TYPE task_field_kind AS ENUM (
    'text',
    'integer',
    'boolean',
    'choice',
    'collection',
    'file'
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

CREATE TYPE verdict_status AS ENUM (
    'ok',
    'wrong_answer',
    'wrong_format',
    'partially_correct',
    'failed',
    'check_failed'
);

ALTER DEFAULT PRIVILEGES FOR ROLE postgres IN SCHEMA public GRANT SELECT, USAGE ON SEQUENCES TO cmslearn_app;

CREATE TABLE IF NOT EXISTS admin_accounts (
    id uuid DEFAULT gen_random_uuid(),
    handle text NOT NULL,
    password_hash text,
    status admin_status DEFAULT 'active'::admin_status NOT NULL,
    created_at timestamptz DEFAULT now() NOT NULL,
    revoked_at timestamptz,
    CONSTRAINT admin_accounts_pkey PRIMARY KEY (id),
    CONSTRAINT admin_accounts_handle_key UNIQUE (handle),
    CONSTRAINT admin_accounts_handle_shape CHECK (handle ~ '^[a-z0-9][a-z0-9_.-]{2,63}$'::text),
    CONSTRAINT admin_accounts_revocation_shape CHECK ((status = 'revoked'::admin_status) = (revoked_at IS NOT NULL))
);

CREATE TABLE IF NOT EXISTS content_seed (
    singleton boolean DEFAULT true,
    version bigint DEFAULT 0 NOT NULL,
    content_hash text DEFAULT '' NOT NULL,
    seeded_at timestamptz,
    CONSTRAINT content_seed_pkey PRIMARY KEY (singleton),
    CONSTRAINT content_seed_single_row CHECK (singleton)
);

CREATE TABLE IF NOT EXISTS levels (
    id text,
    position integer NOT NULL,
    difficulty_min integer NOT NULL,
    difficulty_max integer NOT NULL,
    icon text NOT NULL,
    description_en text DEFAULT '' NOT NULL,
    CONSTRAINT levels_pkey PRIMARY KEY (id),
    CONSTRAINT levels_position_unique UNIQUE (position),
    CONSTRAINT levels_band CHECK (difficulty_min <= difficulty_max),
    CONSTRAINT levels_difficulty_band CHECK (difficulty_min >= 800 AND difficulty_min <= 3500 AND difficulty_max >= 800 AND difficulty_max <= 3500 AND (difficulty_min % 100) = 0 AND (difficulty_max % 100) = 0),
    CONSTRAINT levels_icon_shape CHECK (icon ~ '^[a-z0-9][a-z0-9-]{0,62}$'::text),
    CONSTRAINT levels_id_shape CHECK (id ~ '^[a-z0-9][a-z0-9-]{0,62}$'::text)
);

CREATE UNIQUE INDEX IF NOT EXISTS levels_order_idx ON levels (difficulty_min, difficulty_max);

CREATE TABLE IF NOT EXISTS learner_accounts (
    id uuid DEFAULT gen_random_uuid(),
    serial_id bigint GENERATED ALWAYS AS IDENTITY,
    handle text NOT NULL,
    email text,
    email_verified_at timestamptz,
    password_hash text,
    status principal_status NOT NULL,
    level_id text,
    created_at timestamptz DEFAULT now() NOT NULL,
    CONSTRAINT learner_accounts_pkey PRIMARY KEY (id),
    CONSTRAINT learner_accounts_email_key UNIQUE (email),
    CONSTRAINT learner_accounts_handle_key UNIQUE (handle),
    CONSTRAINT learner_accounts_serial_id_key UNIQUE (serial_id),
    CONSTRAINT learner_accounts_level_id_fkey FOREIGN KEY (level_id) REFERENCES levels (id),
    CONSTRAINT learner_accounts_handle_shape CHECK (handle ~ '^[a-z0-9][a-z0-9_.-]{2,63}$'::text)
);

CREATE TABLE IF NOT EXISTS learner_tokens (
    id uuid DEFAULT gen_random_uuid(),
    learner_id uuid NOT NULL,
    purpose token_purpose NOT NULL,
    token_hash text NOT NULL,
    created_at timestamptz DEFAULT now() NOT NULL,
    expires_at timestamptz NOT NULL,
    used_at timestamptz,
    CONSTRAINT learner_tokens_pkey PRIMARY KEY (id),
    CONSTRAINT learner_tokens_learner_id_fkey FOREIGN KEY (learner_id) REFERENCES learner_accounts (id) ON DELETE CASCADE,
    CONSTRAINT learner_tokens_expiry_after_creation CHECK (expires_at > created_at),
    CONSTRAINT learner_tokens_hash_shape CHECK (token_hash ~ '^[0-9a-f]{64}$'::text)
);

CREATE UNIQUE INDEX IF NOT EXISTS learner_tokens_hash_idx ON learner_tokens (token_hash);

CREATE INDEX IF NOT EXISTS learner_tokens_live_idx ON learner_tokens (learner_id) WHERE (used_at IS NULL);

CREATE TABLE IF NOT EXISTS level_titles (
    level_id text,
    language text,
    title text NOT NULL,
    CONSTRAINT level_titles_pkey PRIMARY KEY (level_id, language),
    CONSTRAINT level_titles_level_id_fkey FOREIGN KEY (level_id) REFERENCES levels (id) ON DELETE CASCADE,
    CONSTRAINT level_titles_language_shape CHECK (language ~ '^[a-z]{2}(-[A-Za-z0-9]{2,8})*$'::text),
    CONSTRAINT level_titles_title_check CHECK (length(btrim(title)) > 0)
);

CREATE TABLE IF NOT EXISTS roles (
    id uuid DEFAULT gen_random_uuid(),
    name text NOT NULL,
    description text DEFAULT '' NOT NULL,
    created_at timestamptz DEFAULT now() NOT NULL,
    CONSTRAINT roles_pkey PRIMARY KEY (id),
    CONSTRAINT roles_name_key UNIQUE (name),
    CONSTRAINT roles_name_check CHECK (name ~ '^[a-z][a-z0-9_]{1,62}$'::text)
);

CREATE TABLE IF NOT EXISTS admin_roles (
    admin_id uuid,
    role_id uuid,
    granted_by uuid,
    granted_at timestamptz DEFAULT now() NOT NULL,
    CONSTRAINT admin_roles_pkey PRIMARY KEY (admin_id, role_id),
    CONSTRAINT admin_roles_admin_id_fkey FOREIGN KEY (admin_id) REFERENCES admin_accounts (id) ON DELETE CASCADE,
    CONSTRAINT admin_roles_granted_by_fkey FOREIGN KEY (granted_by) REFERENCES admin_accounts (id),
    CONSTRAINT admin_roles_role_id_fkey FOREIGN KEY (role_id) REFERENCES roles (id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS role_capabilities (
    role_id uuid,
    capability capability,
    CONSTRAINT role_capabilities_pkey PRIMARY KEY (role_id, capability),
    CONSTRAINT role_capabilities_role_id_fkey FOREIGN KEY (role_id) REFERENCES roles (id) ON DELETE CASCADE
);

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
    actor_kind account_kind NOT NULL,
    actor_id uuid NOT NULL,
    invoked_by text DEFAULT '' NOT NULL,
    CONSTRAINT settings_audit_pkey PRIMARY KEY (id),
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

CREATE TABLE IF NOT EXISTS system_accounts (
    id uuid DEFAULT gen_random_uuid(),
    handle text NOT NULL,
    created_at timestamptz DEFAULT now() NOT NULL,
    CONSTRAINT system_accounts_pkey PRIMARY KEY (id),
    CONSTRAINT system_accounts_handle_key UNIQUE (handle),
    CONSTRAINT system_accounts_handle_shape CHECK (handle ~ '^system:[a-z][a-z0-9_-]{1,62}$'::text)
);

CREATE TABLE IF NOT EXISTS audit_events (
    id bigint GENERATED ALWAYS AS IDENTITY,
    occurred_at timestamptz DEFAULT now() NOT NULL,
    actor_kind account_kind NOT NULL,
    learner_actor_id uuid,
    admin_actor_id uuid,
    system_actor_id uuid,
    invoked_by text NOT NULL,
    action text NOT NULL,
    subject text DEFAULT '' NOT NULL,
    detail jsonb DEFAULT '{}' NOT NULL,
    client_address inet,
    CONSTRAINT audit_events_pkey PRIMARY KEY (id),
    CONSTRAINT audit_events_admin_actor_id_fkey FOREIGN KEY (admin_actor_id) REFERENCES admin_accounts (id),
    CONSTRAINT audit_events_learner_actor_id_fkey FOREIGN KEY (learner_actor_id) REFERENCES learner_accounts (id),
    CONSTRAINT audit_events_system_actor_id_fkey FOREIGN KEY (system_actor_id) REFERENCES system_accounts (id),
    CONSTRAINT audit_events_action_check CHECK (action ~ '^[a-z][a-z0-9_]*(\.[a-z][a-z0-9_]*)+$'::text),
    CONSTRAINT audit_events_invoked_by_check CHECK (length(invoked_by) > 0),
    CONSTRAINT audit_events_one_actor CHECK (actor_kind = 'learner'::account_kind AND learner_actor_id IS NOT NULL AND admin_actor_id IS NULL AND system_actor_id IS NULL OR actor_kind = 'admin'::account_kind AND admin_actor_id IS NOT NULL AND learner_actor_id IS NULL AND system_actor_id IS NULL OR actor_kind = 'system'::account_kind AND system_actor_id IS NOT NULL AND learner_actor_id IS NULL AND admin_actor_id IS NULL)
);

CREATE INDEX IF NOT EXISTS audit_events_admin_actor_idx ON audit_events (admin_actor_id, occurred_at DESC);

CREATE INDEX IF NOT EXISTS audit_events_learner_actor_idx ON audit_events (learner_actor_id, occurred_at DESC);

CREATE TABLE IF NOT EXISTS elevations (
    id uuid DEFAULT gen_random_uuid(),
    admin_id uuid NOT NULL,
    capability capability NOT NULL,
    granted_actor_kind account_kind NOT NULL,
    granted_learner_actor_id uuid,
    granted_admin_actor_id uuid,
    granted_system_actor_id uuid,
    approved_actor_kind account_kind,
    approved_learner_actor_id uuid,
    approved_admin_actor_id uuid,
    approved_system_actor_id uuid,
    granted_at timestamptz DEFAULT now() NOT NULL,
    expires_at timestamptz NOT NULL,
    consumed_at timestamptz,
    reason text NOT NULL,
    CONSTRAINT elevations_pkey PRIMARY KEY (id),
    CONSTRAINT elevations_admin_id_fkey FOREIGN KEY (admin_id) REFERENCES admin_accounts (id) ON DELETE CASCADE,
    CONSTRAINT elevations_approved_admin_actor_id_fkey FOREIGN KEY (approved_admin_actor_id) REFERENCES admin_accounts (id),
    CONSTRAINT elevations_approved_learner_actor_id_fkey FOREIGN KEY (approved_learner_actor_id) REFERENCES learner_accounts (id),
    CONSTRAINT elevations_approved_system_actor_id_fkey FOREIGN KEY (approved_system_actor_id) REFERENCES system_accounts (id),
    CONSTRAINT elevations_granted_admin_actor_id_fkey FOREIGN KEY (granted_admin_actor_id) REFERENCES admin_accounts (id),
    CONSTRAINT elevations_granted_learner_actor_id_fkey FOREIGN KEY (granted_learner_actor_id) REFERENCES learner_accounts (id),
    CONSTRAINT elevations_granted_system_actor_id_fkey FOREIGN KEY (granted_system_actor_id) REFERENCES system_accounts (id),
    CONSTRAINT elevations_approver_shape CHECK (approved_actor_kind IS NULL AND approved_learner_actor_id IS NULL AND approved_admin_actor_id IS NULL AND approved_system_actor_id IS NULL OR approved_actor_kind = 'learner'::account_kind AND approved_learner_actor_id IS NOT NULL AND approved_admin_actor_id IS NULL AND approved_system_actor_id IS NULL OR approved_actor_kind = 'admin'::account_kind AND approved_admin_actor_id IS NOT NULL AND approved_learner_actor_id IS NULL AND approved_system_actor_id IS NULL OR approved_actor_kind = 'system'::account_kind AND approved_system_actor_id IS NOT NULL AND approved_learner_actor_id IS NULL AND approved_admin_actor_id IS NULL),
    CONSTRAINT elevations_expiry_after_grant CHECK (expires_at > granted_at),
    CONSTRAINT elevations_grantor_not_self CHECK (NOT (granted_actor_kind = 'admin'::account_kind AND granted_admin_actor_id = admin_id)),
    CONSTRAINT elevations_grantor_shape CHECK (granted_actor_kind = 'learner'::account_kind AND granted_learner_actor_id IS NOT NULL AND granted_admin_actor_id IS NULL AND granted_system_actor_id IS NULL OR granted_actor_kind = 'admin'::account_kind AND granted_admin_actor_id IS NOT NULL AND granted_learner_actor_id IS NULL AND granted_system_actor_id IS NULL OR granted_actor_kind = 'system'::account_kind AND granted_system_actor_id IS NOT NULL AND granted_learner_actor_id IS NULL AND granted_admin_actor_id IS NULL),
    CONSTRAINT elevations_maximum_minutes CHECK (expires_at <= (granted_at + '01:00:00'::interval)),
    CONSTRAINT elevations_reason_present CHECK (length(btrim(reason)) > 0),
    CONSTRAINT elevations_two_people CHECK (approved_actor_kind IS NULL OR NOT (approved_actor_kind = 'admin'::account_kind AND approved_admin_actor_id = admin_id) AND NOT (approved_actor_kind = granted_actor_kind AND COALESCE(approved_learner_actor_id, approved_admin_actor_id, approved_system_actor_id) = COALESCE(granted_learner_actor_id, granted_admin_actor_id, granted_system_actor_id)))
);

CREATE INDEX IF NOT EXISTS elevations_live_idx ON elevations (admin_id, capability, expires_at) WHERE (consumed_at IS NULL);

CREATE TABLE IF NOT EXISTS sessions (
    id uuid DEFAULT gen_random_uuid(),
    account_kind account_kind NOT NULL,
    learner_id uuid,
    admin_id uuid,
    system_id uuid,
    created_at timestamptz DEFAULT now() NOT NULL,
    last_seen_at timestamptz DEFAULT now() NOT NULL,
    expires_at timestamptz NOT NULL,
    revoked_at timestamptz,
    user_agent text,
    client_address inet,
    CONSTRAINT sessions_pkey PRIMARY KEY (id),
    CONSTRAINT sessions_admin_id_fkey FOREIGN KEY (admin_id) REFERENCES admin_accounts (id) ON DELETE CASCADE,
    CONSTRAINT sessions_learner_id_fkey FOREIGN KEY (learner_id) REFERENCES learner_accounts (id) ON DELETE CASCADE,
    CONSTRAINT sessions_system_id_fkey FOREIGN KEY (system_id) REFERENCES system_accounts (id) ON DELETE CASCADE,
    CONSTRAINT sessions_expiry_after_creation CHECK (expires_at > created_at),
    CONSTRAINT sessions_one_account CHECK (account_kind = 'learner'::account_kind AND learner_id IS NOT NULL AND admin_id IS NULL AND system_id IS NULL OR account_kind = 'admin'::account_kind AND admin_id IS NOT NULL AND learner_id IS NULL AND system_id IS NULL OR account_kind = 'system'::account_kind AND system_id IS NOT NULL AND learner_id IS NULL AND admin_id IS NULL)
);

CREATE INDEX IF NOT EXISTS sessions_live_admin_idx ON sessions (admin_id, expires_at) WHERE (revoked_at IS NULL);

CREATE INDEX IF NOT EXISTS sessions_live_idx ON sessions (learner_id, expires_at) WHERE (revoked_at IS NULL);

CREATE TABLE IF NOT EXISTS settings (
    key text,
    kind setting_kind NOT NULL,
    value_text text NOT NULL,
    tier setting_tier DEFAULT 'operator'::setting_tier NOT NULL,
    description text DEFAULT '' NOT NULL,
    updated_at timestamptz DEFAULT now() NOT NULL,
    actor_kind account_kind NOT NULL,
    learner_actor_id uuid,
    admin_actor_id uuid,
    system_actor_id uuid,
    CONSTRAINT settings_pkey PRIMARY KEY (key),
    CONSTRAINT settings_admin_actor_id_fkey FOREIGN KEY (admin_actor_id) REFERENCES admin_accounts (id),
    CONSTRAINT settings_learner_actor_id_fkey FOREIGN KEY (learner_actor_id) REFERENCES learner_accounts (id),
    CONSTRAINT settings_system_actor_id_fkey FOREIGN KEY (system_actor_id) REFERENCES system_accounts (id),
    CONSTRAINT settings_key_shape CHECK (key ~ '^[a-z][a-z0-9_]*(\.[a-z][a-z0-9_]*){1,7}$'::text),
    CONSTRAINT settings_one_actor CHECK (actor_kind = 'learner'::account_kind AND learner_actor_id IS NOT NULL AND admin_actor_id IS NULL AND system_actor_id IS NULL OR actor_kind = 'admin'::account_kind AND admin_actor_id IS NOT NULL AND learner_actor_id IS NULL AND system_actor_id IS NULL OR actor_kind = 'system'::account_kind AND system_actor_id IS NOT NULL AND learner_actor_id IS NULL AND admin_actor_id IS NULL),
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

CREATE TABLE IF NOT EXISTS settings_plans (
    id uuid DEFAULT gen_random_uuid(),
    actor_kind account_kind NOT NULL,
    learner_actor_id uuid,
    admin_actor_id uuid,
    system_actor_id uuid,
    changes jsonb NOT NULL,
    created_at timestamptz DEFAULT now() NOT NULL,
    expires_at timestamptz DEFAULT (now() + '00:15:00'::interval) NOT NULL,
    applied_at timestamptz,
    CONSTRAINT settings_plans_pkey PRIMARY KEY (id),
    CONSTRAINT settings_plans_admin_actor_id_fkey FOREIGN KEY (admin_actor_id) REFERENCES admin_accounts (id),
    CONSTRAINT settings_plans_learner_actor_id_fkey FOREIGN KEY (learner_actor_id) REFERENCES learner_accounts (id),
    CONSTRAINT settings_plans_system_actor_id_fkey FOREIGN KEY (system_actor_id) REFERENCES system_accounts (id),
    CONSTRAINT settings_plans_expiry_after_creation CHECK (expires_at > created_at),
    CONSTRAINT settings_plans_one_actor CHECK (actor_kind = 'learner'::account_kind AND learner_actor_id IS NOT NULL AND admin_actor_id IS NULL AND system_actor_id IS NULL OR actor_kind = 'admin'::account_kind AND admin_actor_id IS NOT NULL AND learner_actor_id IS NULL AND system_actor_id IS NULL OR actor_kind = 'system'::account_kind AND system_actor_id IS NOT NULL AND learner_actor_id IS NULL AND admin_actor_id IS NULL)
);

CREATE TABLE IF NOT EXISTS task_types (
    id text,
    title_en text NOT NULL,
    title_th text DEFAULT '' NOT NULL,
    position integer NOT NULL,
    created_at timestamptz DEFAULT now() NOT NULL,
    CONSTRAINT task_types_pkey PRIMARY KEY (id),
    CONSTRAINT task_types_position_unique UNIQUE (position),
    CONSTRAINT task_types_id_shape CHECK (id ~ '^[a-z][a-z0-9_]{1,62}$'::text),
    CONSTRAINT task_types_title_en_check CHECK (length(btrim(title_en)) > 0)
);

CREATE TABLE IF NOT EXISTS task_field_defs (
    task_type_id text,
    key text,
    kind task_field_kind NOT NULL,
    label_en text NOT NULL,
    label_th text DEFAULT '' NOT NULL,
    required boolean DEFAULT false NOT NULL,
    choices jsonb,
    members jsonb,
    minimum integer,
    maximum integer,
    position integer NOT NULL,
    CONSTRAINT task_field_defs_pkey PRIMARY KEY (task_type_id, key),
    CONSTRAINT task_field_defs_position_unique UNIQUE (task_type_id, position),
    CONSTRAINT task_field_defs_task_type_id_fkey FOREIGN KEY (task_type_id) REFERENCES task_types (id) ON DELETE CASCADE,
    CONSTRAINT task_field_defs_bounds CHECK ((minimum IS NOT NULL OR maximum IS NOT NULL) = (kind = 'integer'::task_field_kind)),
    CONSTRAINT task_field_defs_bounds_order CHECK (minimum IS NULL OR maximum IS NULL OR minimum <= maximum),
    CONSTRAINT task_field_defs_choices CHECK ((kind = 'choice'::task_field_kind) = (choices IS NOT NULL)),
    CONSTRAINT task_field_defs_choices_shape CHECK (choices IS NULL OR jsonb_typeof(choices) = 'array'::text AND jsonb_array_length(choices) >= 2),
    CONSTRAINT task_field_defs_key_shape CHECK (key ~ '^[a-z][a-z0-9_]{1,62}$'::text),
    CONSTRAINT task_field_defs_label_en_check CHECK (length(btrim(label_en)) > 0),
    CONSTRAINT task_field_defs_members CHECK (kind <> 'collection'::task_field_kind OR members IS NOT NULL),
    CONSTRAINT task_field_defs_members_shape CHECK (members IS NULL OR jsonb_typeof(members) = 'array'::text AND jsonb_array_length(members) >= 1)
);

CREATE TABLE IF NOT EXISTS task_visibility (
    task_id text,
    principal_id bigint,
    CONSTRAINT task_visibility_pkey PRIMARY KEY (task_id, principal_id),
    CONSTRAINT task_visibility_principal_range CHECK (principal_id = 0 OR principal_id >= (1::bigint << 48) AND principal_id < (2::bigint << 48) OR principal_id >= (2::bigint << 48) AND principal_id < (3::bigint << 48) OR principal_id >= (3::bigint << 48) AND principal_id < (4::bigint << 48))
);

CREATE TABLE IF NOT EXISTS tasks (
    id text,
    version integer,
    task_type text,
    status task_status NOT NULL,
    title_en text NOT NULL,
    title_th text NOT NULL,
    difficulty integer NOT NULL,
    time_limit_ms integer NOT NULL,
    memory_limit_mb integer NOT NULL,
    points integer DEFAULT 100 NOT NULL,
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
    CONSTRAINT tasks_points_check CHECK (points >= 1 AND points <= 1000),
    CONSTRAINT tasks_time_limit CHECK (time_limit_ms >= 100 AND time_limit_ms <= 20000),
    CONSTRAINT tasks_title_en_check CHECK (length(btrim(title_en)) > 0),
    CONSTRAINT tasks_title_th_check CHECK (length(btrim(title_th)) > 0)
);

CREATE INDEX IF NOT EXISTS tasks_published_idx ON tasks (difficulty, id) WHERE (status = 'published'::task_status);

CREATE TABLE IF NOT EXISTS generation_jobs (
    id bigint GENERATED ALWAYS AS IDENTITY,
    task_id text NOT NULL,
    task_version integer NOT NULL,
    state generation_state DEFAULT 'queued'::generation_state NOT NULL,
    attempts integer DEFAULT 0 NOT NULL,
    claimed_by text,
    claimed_at timestamptz,
    lease_expires_at timestamptz,
    created_at timestamptz DEFAULT now() NOT NULL,
    updated_at timestamptz DEFAULT now() NOT NULL,
    CONSTRAINT generation_jobs_pkey PRIMARY KEY (id),
    CONSTRAINT generation_jobs_task_version_unique UNIQUE (task_id, task_version),
    CONSTRAINT generation_jobs_task_fkey FOREIGN KEY (task_id, task_version) REFERENCES tasks (id, version) ON DELETE CASCADE,
    CONSTRAINT generation_jobs_claim CHECK ((claimed_by IS NULL) = (claimed_at IS NULL) AND (claimed_by IS NULL) = (lease_expires_at IS NULL))
);

CREATE INDEX IF NOT EXISTS generation_jobs_lease_idx ON generation_jobs (lease_expires_at) WHERE (state = 'running'::generation_state);

CREATE INDEX IF NOT EXISTS generation_jobs_queue_idx ON generation_jobs (created_at) WHERE (state = 'queued'::generation_state);

CREATE TABLE IF NOT EXISTS generation_artefacts (
    id bigint GENERATED ALWAYS AS IDENTITY,
    job_id bigint NOT NULL,
    step smallint NOT NULL,
    kind text NOT NULL,
    locale text,
    object_key text,
    inline_text text,
    digest text NOT NULL,
    model_id text NOT NULL,
    prompt_version text NOT NULL,
    seed bigint NOT NULL,
    call_index integer NOT NULL,
    created_at timestamptz DEFAULT now() NOT NULL,
    CONSTRAINT generation_artefacts_pkey PRIMARY KEY (id),
    CONSTRAINT generation_artefacts_call_unique UNIQUE NULLS NOT DISTINCT (job_id, step, kind, locale, call_index),
    CONSTRAINT generation_artefacts_job_id_fkey FOREIGN KEY (job_id) REFERENCES generation_jobs (id) ON DELETE CASCADE,
    CONSTRAINT generation_artefacts_digest_check CHECK (digest ~ '^[0-9a-f]{64}$'::text),
    CONSTRAINT generation_artefacts_kind_check CHECK (length(btrim(kind)) > 0),
    CONSTRAINT generation_artefacts_payload CHECK (object_key IS NOT NULL OR inline_text IS NOT NULL),
    CONSTRAINT generation_artefacts_step_check CHECK (step >= 1 AND step <= 7)
);

CREATE INDEX IF NOT EXISTS generation_artefacts_job_idx ON generation_artefacts (job_id, step);

CREATE TABLE IF NOT EXISTS generation_candidates (
    id bigint GENERATED ALWAYS AS IDENTITY,
    job_id bigint NOT NULL,
    position integer NOT NULL,
    approach text NOT NULL,
    intended_complexity text NOT NULL,
    fits_bounds boolean NOT NULL,
    plan jsonb NOT NULL,
    chosen boolean DEFAULT false NOT NULL,
    reason text NOT NULL,
    call_artefact_id bigint NOT NULL,
    CONSTRAINT generation_candidates_pkey PRIMARY KEY (id),
    CONSTRAINT generation_candidates_job_id_position_key UNIQUE (job_id, position),
    CONSTRAINT generation_candidates_call_fkey FOREIGN KEY (call_artefact_id) REFERENCES generation_artefacts (id),
    CONSTRAINT generation_candidates_job_id_fkey FOREIGN KEY (job_id) REFERENCES generation_jobs (id) ON DELETE CASCADE,
    CONSTRAINT generation_candidates_approach_check CHECK (length(btrim(approach)) > 0),
    CONSTRAINT generation_candidates_position_check CHECK ("position" > 0),
    CONSTRAINT generation_candidates_reason_check CHECK (length(btrim(reason)) > 0)
);

CREATE TABLE IF NOT EXISTS generation_specifications (
    job_id bigint,
    specification jsonb NOT NULL,
    time_limit_ms integer NOT NULL,
    memory_limit_mb integer NOT NULL,
    derivation text NOT NULL,
    answer_artefact_id bigint NOT NULL,
    oracle_artefact_id bigint NOT NULL,
    created_at timestamptz DEFAULT now() NOT NULL,
    CONSTRAINT generation_specifications_pkey PRIMARY KEY (job_id),
    CONSTRAINT generation_specifications_answer_artefact_id_fkey FOREIGN KEY (answer_artefact_id) REFERENCES generation_artefacts (id),
    CONSTRAINT generation_specifications_job_id_fkey FOREIGN KEY (job_id) REFERENCES generation_jobs (id) ON DELETE CASCADE,
    CONSTRAINT generation_specifications_oracle_artefact_id_fkey FOREIGN KEY (oracle_artefact_id) REFERENCES generation_artefacts (id),
    CONSTRAINT generation_specifications_calls_distinct CHECK (answer_artefact_id <> oracle_artefact_id),
    CONSTRAINT generation_specifications_derivation_check CHECK (length(btrim(derivation)) > 0),
    CONSTRAINT generation_specifications_memory_limit_mb_check CHECK (memory_limit_mb >= 16 AND memory_limit_mb <= 1024),
    CONSTRAINT generation_specifications_time_limit_ms_check CHECK (time_limit_ms >= 100 AND time_limit_ms <= 20000)
);

CREATE TABLE IF NOT EXISTS progress (
    id bigint GENERATED ALWAYS AS IDENTITY,
    account_id uuid NOT NULL,
    task_id text NOT NULL,
    task_version integer NOT NULL,
    status progress_status DEFAULT 'attempted'::progress_status NOT NULL,
    attempted_at timestamptz DEFAULT now() NOT NULL,
    solved_at timestamptz,
    CONSTRAINT progress_pkey PRIMARY KEY (id),
    CONSTRAINT progress_account_id_fkey FOREIGN KEY (account_id) REFERENCES learner_accounts (id) ON DELETE CASCADE,
    CONSTRAINT progress_task_fkey FOREIGN KEY (task_id, task_version) REFERENCES tasks (id, version) ON DELETE CASCADE,
    CONSTRAINT progress_solved_has_time CHECK ((status = 'solved'::progress_status) = (solved_at IS NOT NULL))
);

CREATE UNIQUE INDEX IF NOT EXISTS progress_account_task_idx ON progress (account_id, task_id, task_version);

CREATE TABLE IF NOT EXISTS submissions (
    id uuid DEFAULT gen_random_uuid(),
    account_id uuid NOT NULL,
    task_id text NOT NULL,
    task_version integer NOT NULL,
    source text NOT NULL,
    submitted_at timestamptz DEFAULT now() NOT NULL,
    verdict verdict_status,
    verdict_detail text,
    judged_at timestamptz,
    claimed_by text,
    claimed_at timestamptz,
    CONSTRAINT submissions_pkey PRIMARY KEY (id),
    CONSTRAINT submissions_account_id_fkey FOREIGN KEY (account_id) REFERENCES learner_accounts (id) ON DELETE CASCADE,
    CONSTRAINT submissions_task_fkey FOREIGN KEY (task_id, task_version) REFERENCES tasks (id, version) ON DELETE CASCADE,
    CONSTRAINT submissions_claim_complete CHECK ((claimed_by IS NULL) = (claimed_at IS NULL)),
    CONSTRAINT submissions_source_check CHECK (length(source) > 0),
    CONSTRAINT submissions_verdict_has_time CHECK ((verdict IS NULL) = (judged_at IS NULL))
);

CREATE INDEX IF NOT EXISTS submissions_account_idx ON submissions (account_id, submitted_at DESC);

CREATE INDEX IF NOT EXISTS submissions_unjudged_idx ON submissions (submitted_at) WHERE (verdict IS NULL);

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

CREATE TABLE IF NOT EXISTS task_field_values (
    task_id text,
    task_version integer,
    field_key text,
    value jsonb NOT NULL,
    CONSTRAINT task_field_values_pkey PRIMARY KEY (task_id, task_version, field_key),
    CONSTRAINT task_field_values_task_fkey FOREIGN KEY (task_id, task_version) REFERENCES tasks (id, version) ON DELETE CASCADE
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

CREATE TABLE IF NOT EXISTS task_subtasks (
    task_id text,
    task_version integer,
    position integer,
    points integer NOT NULL,
    constraint_text text NOT NULL,
    CONSTRAINT task_subtasks_pkey PRIMARY KEY (task_id, task_version, position),
    CONSTRAINT task_subtasks_task_fkey FOREIGN KEY (task_id, task_version) REFERENCES tasks (id, version) ON DELETE CASCADE,
    CONSTRAINT task_subtasks_constraint_text_check CHECK (length(btrim(constraint_text)) > 0),
    CONSTRAINT task_subtasks_points_check CHECK (points > 0 AND points <= 1000),
    CONSTRAINT task_subtasks_position_check CHECK ("position" > 0)
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
RETURNS actor_identity
LANGUAGE plpgsql
STABLE
SET search_path = pg_catalog, public
AS $$
DECLARE
  raw_id text := current_setting('cms.actor_id', true);
  raw_kind text := current_setting('cms.actor_kind', true);
BEGIN
  IF raw_id IS NULL OR raw_id = '' THEN
    RAISE EXCEPTION 'cms.actor_id is not set; every settings write must name its actor'
      USING ERRCODE = 'insufficient_privilege';
  END IF;
  IF raw_kind IS NULL OR raw_kind = '' THEN
    RAISE EXCEPTION 'cms.actor_kind is not set; every settings write must name the actor''s kind'
      USING ERRCODE = 'insufficient_privilege';
  END IF;
  RETURN (raw_kind::account_kind, raw_id::uuid);
END;
$$;

CREATE OR REPLACE FUNCTION file_field_advice(
    field_key text
)
RETURNS text
LANGUAGE sql
IMMUTABLE
AS $$
  SELECT 'the field ' || field_key || ' needs the id of a stored artefact, not the file''s text; ' ||
         'the generation pipeline writes that id when it stores the file, so store the file there first, ' ||
         'then save the draft again'
$$;

CREATE OR REPLACE FUNCTION file_field_unknown_artefact(
    field_key text,
    artefact_id bigint
)
RETURNS text
LANGUAGE sql
IMMUTABLE
AS $$
  SELECT 'artefact ' || artefact_id || ' is not a stored object the field ' || field_key ||
         ' can use: it is either not recorded or recorded without an object; ' ||
         'the generation pipeline stores the file and records its id, so store the file there first, ' ||
         'then save the draft again'
$$;

CREATE OR REPLACE FUNCTION generation_artefact_is_cited()
RETURNS trigger
LANGUAGE plpgsql
VOLATILE
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

CREATE OR REPLACE FUNCTION generation_candidates_are_one_decision()
RETURNS trigger
LANGUAGE plpgsql
VOLATILE
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

CREATE OR REPLACE FUNCTION generation_job_moves_forward()
RETURNS trigger
LANGUAGE plpgsql
VOLATILE
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

CREATE OR REPLACE FUNCTION levels_partition_the_scale()
RETURNS trigger
LANGUAGE plpgsql
VOLATILE
AS $$
DECLARE
  expected integer := 800;
  current_min integer;
BEGIN
  FOR current_min IN SELECT difficulty_min FROM levels ORDER BY difficulty_min LOOP
    IF current_min <> expected THEN
      RAISE EXCEPTION 'level list is not a partition of 800..3500: a band starts at %, expected %',
        current_min, expected;
    END IF;
    expected := (SELECT difficulty_max + 100 FROM levels WHERE difficulty_min = current_min);
  END LOOP;
  IF expected - 100 <> 3500 THEN
    RAISE EXCEPTION 'level list is not a partition of 800..3500: the last band ends at %, expected 3500',
      expected - 100;
  END IF;
  RETURN NULL;
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
  actor actor_identity := current_actor();
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
                              new_tier, actor_kind, actor_id, invoked_by)
  VALUES (changed_key, lower(TG_OP)::audit_operation, old_kind, new_kind, old_value, new_value,
          old_tier, new_tier, actor.kind, actor.id,
          coalesce(current_setting('cms.invoked_by', true), ''));
  RETURN NULL;
END;
$$;

CREATE OR REPLACE FUNCTION task_field_audit()
RETURNS trigger
LANGUAGE plpgsql
VOLATILE
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

CREATE OR REPLACE FUNCTION task_field_defs_refuse_orphaning()
RETURNS trigger
LANGUAGE plpgsql
VOLATILE
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

CREATE OR REPLACE FUNCTION task_field_members_exist()
RETURNS trigger
LANGUAGE plpgsql
VOLATILE
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

CREATE OR REPLACE FUNCTION task_field_value_is_artefact_id()
RETURNS trigger
LANGUAGE plpgsql
VOLATILE
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
    RAISE EXCEPTION '%', file_field_advice(NEW.field_key)
      USING ERRCODE = 'check_violation';
  END IF;
  artefact_id := (NEW.value #>> '{}')::bigint;
  IF NOT EXISTS (SELECT 1 FROM generation_artefacts a
                  WHERE a.id = artefact_id AND a.object_key IS NOT NULL) THEN
    RAISE EXCEPTION '%', file_field_unknown_artefact(NEW.field_key, artefact_id)
      USING ERRCODE = 'foreign_key_violation';
  END IF;
  RETURN NEW;
END;
$$;

CREATE OR REPLACE FUNCTION task_field_value_is_filled()
RETURNS trigger
LANGUAGE plpgsql
VOLATILE
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

CREATE OR REPLACE FUNCTION task_points_keep_the_partition()
RETURNS trigger
LANGUAGE plpgsql
VOLATILE
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

CREATE OR REPLACE FUNCTION task_published_needs_its_subtasks()
RETURNS trigger
LANGUAGE plpgsql
VOLATILE
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

CREATE OR REPLACE FUNCTION task_subtasks_are_a_partition()
RETURNS trigger
LANGUAGE plpgsql
VOLATILE
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

CREATE OR REPLACE FUNCTION task_type_audit()
RETURNS trigger
LANGUAGE plpgsql
VOLATILE
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

CREATE OR REPLACE TRIGGER audit_events_append_only
    BEFORE UPDATE OR DELETE ON audit_events
    FOR EACH ROW
    EXECUTE FUNCTION reject_mutation();

CREATE OR REPLACE TRIGGER generation_artefacts_are_cited
    BEFORE DELETE ON generation_artefacts
    FOR EACH ROW
    EXECUTE FUNCTION generation_artefact_is_cited();

CREATE CONSTRAINT TRIGGER generation_candidates_one_decision
    AFTER INSERT OR UPDATE ON generation_candidates
    DEFERRABLE INITIALLY DEFERRED
    FOR EACH ROW
    EXECUTE FUNCTION generation_candidates_are_one_decision();

CREATE OR REPLACE TRIGGER generation_jobs_move_forward
    BEFORE UPDATE ON generation_jobs
    FOR EACH ROW
    EXECUTE FUNCTION generation_job_moves_forward();

CREATE CONSTRAINT TRIGGER levels_partition_check
    AFTER INSERT OR UPDATE OR DELETE ON levels
    DEFERRABLE INITIALLY DEFERRED
    FOR EACH ROW
    EXECUTE FUNCTION levels_partition_the_scale();

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

CREATE OR REPLACE TRIGGER task_field_defs_audit
    AFTER INSERT OR UPDATE OR DELETE ON task_field_defs
    FOR EACH ROW
    EXECUTE FUNCTION task_field_audit();

CREATE OR REPLACE TRIGGER task_field_defs_members_exist
    BEFORE INSERT OR UPDATE ON task_field_defs
    FOR EACH ROW
    EXECUTE FUNCTION task_field_members_exist();

CREATE OR REPLACE TRIGGER task_field_defs_refuse_orphaning
    BEFORE DELETE ON task_field_defs
    FOR EACH ROW
    EXECUTE FUNCTION task_field_defs_refuse_orphaning();

CREATE OR REPLACE TRIGGER task_field_values_artefact_id
    BEFORE INSERT OR UPDATE ON task_field_values
    FOR EACH ROW
    EXECUTE FUNCTION task_field_value_is_artefact_id();

CREATE OR REPLACE TRIGGER task_field_values_filled
    BEFORE INSERT OR UPDATE ON task_field_values
    FOR EACH ROW
    EXECUTE FUNCTION task_field_value_is_filled();

CREATE CONSTRAINT TRIGGER task_subtasks_follow_the_total
    AFTER UPDATE ON tasks
    DEFERRABLE INITIALLY DEFERRED
    FOR EACH ROW
    EXECUTE FUNCTION task_points_keep_the_partition();

CREATE CONSTRAINT TRIGGER task_subtasks_partition
    AFTER INSERT OR UPDATE OR DELETE ON task_subtasks
    DEFERRABLE INITIALLY DEFERRED
    FOR EACH ROW
    EXECUTE FUNCTION task_subtasks_are_a_partition();

CREATE OR REPLACE TRIGGER task_types_audit
    AFTER INSERT OR UPDATE OR DELETE ON task_types
    FOR EACH ROW
    EXECUTE FUNCTION task_type_audit();

CREATE OR REPLACE TRIGGER tasks_published_immutable
    BEFORE UPDATE ON tasks
    FOR EACH ROW
    EXECUTE FUNCTION reject_published_task_change();

CREATE OR REPLACE TRIGGER tasks_published_needs_subtasks
    BEFORE INSERT OR UPDATE OF status ON tasks
    FOR EACH ROW
    EXECUTE FUNCTION task_published_needs_its_subtasks();

REVOKE EXECUTE ON FUNCTION current_actor() FROM PUBLIC;

REVOKE EXECUTE ON FUNCTION settings_bump_version() FROM PUBLIC;

REVOKE EXECUTE ON FUNCTION settings_write_audit() FROM PUBLIC;

GRANT EXECUTE ON FUNCTION current_actor() TO cmslearn_app;

GRANT SELECT, USAGE ON SEQUENCE audit_events_id_seq TO cmslearn_app;

GRANT SELECT, USAGE ON SEQUENCE generation_artefacts_id_seq TO cmslearn_app;

GRANT SELECT, USAGE ON SEQUENCE generation_candidates_id_seq TO cmslearn_app;

GRANT SELECT, USAGE ON SEQUENCE generation_jobs_id_seq TO cmslearn_app;

GRANT SELECT, USAGE ON SEQUENCE learner_accounts_serial_id_seq TO cmslearn_app;

GRANT SELECT, USAGE ON SEQUENCE progress_id_seq TO cmslearn_app;

GRANT SELECT, USAGE ON SEQUENCE settings_audit_id_seq TO cmslearn_app;

GRANT INSERT, SELECT, UPDATE ON TABLE admin_accounts TO cmslearn_app;

GRANT DELETE, INSERT, SELECT ON TABLE admin_roles TO cmslearn_app;

GRANT INSERT, SELECT ON TABLE audit_events TO cmslearn_app;

GRANT INSERT, SELECT, UPDATE ON TABLE content_seed TO cmslearn_app;

GRANT INSERT, SELECT, UPDATE ON TABLE elevations TO cmslearn_app;

GRANT INSERT, SELECT ON TABLE generation_artefacts TO cmslearn_generator;

GRANT INSERT, SELECT ON TABLE generation_candidates TO cmslearn_generator;

GRANT INSERT, SELECT, UPDATE ON TABLE generation_jobs TO cmslearn_generator;

GRANT INSERT, SELECT ON TABLE generation_specifications TO cmslearn_generator;

GRANT INSERT, SELECT, UPDATE ON TABLE learner_accounts TO cmslearn_app;

GRANT INSERT, SELECT, UPDATE ON TABLE learner_tokens TO cmslearn_app;

GRANT DELETE, INSERT, SELECT, UPDATE ON TABLE level_titles TO cmslearn_app;

GRANT DELETE, INSERT, SELECT, UPDATE ON TABLE levels TO cmslearn_app;

GRANT DELETE, INSERT, SELECT, UPDATE ON TABLE progress TO cmslearn_app;

GRANT INSERT, SELECT, UPDATE ON TABLE progress TO cmslearn_judge;

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

GRANT DELETE, INSERT, SELECT, UPDATE ON TABLE submissions TO cmslearn_app;

GRANT SELECT, UPDATE ON TABLE submissions TO cmslearn_judge;

GRANT INSERT, SELECT, UPDATE ON TABLE system_accounts TO cmslearn_app;

GRANT DELETE, INSERT, SELECT, UPDATE ON TABLE task_examples TO cmslearn_app;

GRANT SELECT ON TABLE task_examples TO cmslearn_judge;

GRANT DELETE, INSERT, SELECT, UPDATE ON TABLE task_field_defs TO cmslearn_app;

GRANT SELECT ON TABLE task_field_defs TO cmslearn_generator;

GRANT DELETE, INSERT, SELECT, UPDATE ON TABLE task_field_values TO cmslearn_app;

GRANT SELECT ON TABLE task_field_values TO cmslearn_generator;

GRANT DELETE, INSERT, SELECT, UPDATE ON TABLE task_statements TO cmslearn_app;

GRANT DELETE, INSERT, SELECT, UPDATE ON TABLE task_topics TO cmslearn_app;

GRANT SELECT ON TABLE task_topics TO cmslearn_generator;

GRANT DELETE, INSERT, SELECT, UPDATE ON TABLE task_types TO cmslearn_app;

GRANT SELECT ON TABLE task_types TO cmslearn_generator;

GRANT DELETE, INSERT, SELECT, UPDATE ON TABLE task_visibility TO cmslearn_app;

GRANT SELECT ON TABLE task_visibility TO cmslearn_judge;

GRANT DELETE, INSERT, SELECT, UPDATE ON TABLE tasks TO cmslearn_app;

GRANT SELECT ON TABLE tasks TO cmslearn_judge;

GRANT DELETE, INSERT, SELECT, UPDATE ON TABLE topics TO cmslearn_app;

GRANT SELECT ON TABLE topics TO cmslearn_generator;

GRANT DELETE, INSERT, SELECT, UPDATE ON TABLE track_stages TO cmslearn_app;

GRANT DELETE, INSERT, SELECT, UPDATE ON TABLE tracks TO cmslearn_app;

GRANT USAGE ON TYPE task_field_kind TO cmslearn_app;

GRANT SELECT (digest, id, object_key) ON TABLE generation_artefacts TO cmslearn_app;

GRANT SELECT (checker, checker_path, content_hash, difficulty, id, memory_limit_mb, origin, seeded, status, task_type, time_limit_ms, title_en, title_th, verification, version) ON TABLE tasks TO cmslearn_generator;
