CREATE TYPE setting_tier AS ENUM ('public', 'operator', 'secret');

CREATE TABLE settings (
  key text PRIMARY KEY,
  kind setting_kind NOT NULL,
  value_text text NOT NULL,
  tier setting_tier NOT NULL DEFAULT 'operator',
  description text NOT NULL DEFAULT '',
  updated_at timestamptz NOT NULL DEFAULT now(),
  updated_by uuid NOT NULL REFERENCES principals (id),
  CONSTRAINT settings_key_shape CHECK (key ~ '^[a-z][a-z0-9_]*(\.[a-z][a-z0-9_]*){1,7}$'),
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
  author_id uuid NOT NULL REFERENCES principals (id),
  changes jsonb NOT NULL,
  created_at timestamptz NOT NULL DEFAULT now(),
  expires_at timestamptz NOT NULL DEFAULT now() + interval '15 minutes',
  applied_at timestamptz,
  CONSTRAINT settings_plans_expiry_after_creation CHECK (expires_at > created_at)
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
  actor_id uuid NOT NULL REFERENCES principals (id),
  invoked_by text NOT NULL DEFAULT '',
  CONSTRAINT settings_audit_update_has_old_state
    CHECK (operation <> 'update' OR (old_kind IS NOT NULL AND new_kind IS NOT NULL))
);

CREATE INDEX settings_audit_key_idx ON settings_audit (key, occurred_at DESC);
