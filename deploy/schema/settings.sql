CREATE TYPE setting_tier AS ENUM ('public', 'operator', 'secret');

CREATE TABLE settings (
  key text PRIMARY KEY,
  kind setting_kind NOT NULL,
  value_text text NOT NULL,
  tier setting_tier NOT NULL DEFAULT 'operator',
  description text NOT NULL DEFAULT '',
  updated_at timestamptz NOT NULL DEFAULT now(),
  actor_kind account_kind NOT NULL,
  learner_actor_id uuid REFERENCES learner_accounts (id),
  admin_actor_id uuid REFERENCES admin_accounts (id),
  system_actor_id uuid REFERENCES system_accounts (id),
  CONSTRAINT settings_key_shape CHECK (key ~ '^[a-z][a-z0-9_]*(\.[a-z][a-z0-9_]*){1,7}$'),
  -- A secret is never stored as plain text (ADR 0054 §4): the api seals it before the row exists, and this is where every
  -- writer must pass. The prefix is the shape core writes, so a value sealed by anything else is refused here instead of
  -- being read back as if it were a secret.
  CONSTRAINT settings_secret_is_sealed CHECK (tier <> 'secret' OR value_text LIKE 'sealed:v1:%'),
  CONSTRAINT settings_one_actor CHECK (
    (actor_kind = 'learner' AND learner_actor_id IS NOT NULL
      AND admin_actor_id IS NULL AND system_actor_id IS NULL)
    OR (actor_kind = 'admin' AND admin_actor_id IS NOT NULL
      AND learner_actor_id IS NULL AND system_actor_id IS NULL)
    OR (actor_kind = 'system' AND system_actor_id IS NOT NULL
      AND learner_actor_id IS NULL AND admin_actor_id IS NULL)
  ),
  -- Nested CASE: the cast runs only on text the WHEN already proved is an integer.
  CONSTRAINT settings_value_matches_kind CHECK (
    CASE kind
      WHEN 'bool' THEN value_text IN ('true', 'false')
      WHEN 'int' THEN
        CASE
          WHEN value_text ~ '^-?(0|[1-9][0-9]{0,18})$'
            THEN value_text::numeric BETWEEN -9223372036854775808 AND 9223372036854775807
          ELSE false
        END
      WHEN 'text' THEN true
    END
  )
);

CREATE TABLE settings_meta (
  singleton boolean PRIMARY KEY DEFAULT true,
  version bigint NOT NULL DEFAULT 1,
  updated_at timestamptz NOT NULL DEFAULT now(),
  CONSTRAINT settings_meta_single_row CHECK (singleton)
);

CREATE TABLE settings_plans (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  actor_kind account_kind NOT NULL,
  learner_actor_id uuid REFERENCES learner_accounts (id),
  admin_actor_id uuid REFERENCES admin_accounts (id),
  system_actor_id uuid REFERENCES system_accounts (id),
  changes jsonb NOT NULL,
  created_at timestamptz NOT NULL DEFAULT now(),
  expires_at timestamptz NOT NULL DEFAULT now() + interval '15 minutes',
  applied_at timestamptz,
  CONSTRAINT settings_plans_expiry_after_creation CHECK (expires_at > created_at),
  CONSTRAINT settings_plans_one_actor CHECK (
    (actor_kind = 'learner' AND learner_actor_id IS NOT NULL
      AND admin_actor_id IS NULL AND system_actor_id IS NULL)
    OR (actor_kind = 'admin' AND admin_actor_id IS NOT NULL
      AND learner_actor_id IS NULL AND system_actor_id IS NULL)
    OR (actor_kind = 'system' AND system_actor_id IS NOT NULL
      AND learner_actor_id IS NULL AND admin_actor_id IS NULL)
  )
);

CREATE TABLE settings_audit (
  id bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
  occurred_at timestamptz NOT NULL DEFAULT now(),
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
  invoked_by text NOT NULL DEFAULT '',
  CONSTRAINT settings_audit_update_has_old_state
    CHECK (operation <> 'update' OR (old_kind IS NOT NULL AND new_kind IS NOT NULL))
);

CREATE INDEX settings_audit_key_idx ON settings_audit (key, occurred_at DESC);
