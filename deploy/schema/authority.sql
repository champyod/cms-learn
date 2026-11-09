-- Administrator accounts are created by the command line or an existing administrator, never by
-- self-registration, so they carry no email: an email exists in one table only and no cross-table
-- uniqueness rule is needed (ADR 0016).
CREATE TABLE admin_accounts (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  handle text NOT NULL UNIQUE,
  password_hash text,
  status admin_status NOT NULL DEFAULT 'active',
  created_at timestamptz NOT NULL DEFAULT now(),
  revoked_at timestamptz,
  CONSTRAINT admin_accounts_handle_shape CHECK (handle ~ '^[a-z0-9][a-z0-9_.-]{2,63}$'),
  CONSTRAINT admin_accounts_revocation_shape CHECK ((status = 'revoked') = (revoked_at IS NOT NULL))
);

CREATE TABLE learner_accounts (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  -- The one numeric id (ADR 0010): a task's encoded audience value carries this, not the uuid.
  serial_id bigint GENERATED ALWAYS AS IDENTITY UNIQUE,
  handle text NOT NULL UNIQUE,
  email text UNIQUE,
  email_verified_at timestamptz,
  password_hash text,
  status principal_status NOT NULL,
  level_id text REFERENCES levels (id),
  created_at timestamptz NOT NULL DEFAULT now(),
  CONSTRAINT learner_accounts_handle_shape CHECK (handle ~ '^[a-z0-9][a-z0-9_.-]{2,63}$')
);

CREATE TABLE system_accounts (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  handle text NOT NULL UNIQUE,
  created_at timestamptz NOT NULL DEFAULT now(),
  CONSTRAINT system_accounts_handle_shape CHECK (handle ~ '^system:[a-z][a-z0-9_-]{1,62}$')
);

CREATE TABLE roles (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  name text NOT NULL UNIQUE CHECK (name ~ '^[a-z][a-z0-9_]{1,62}$'),
  description text NOT NULL DEFAULT '',
  created_at timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE role_capabilities (
  role_id uuid NOT NULL REFERENCES roles (id) ON DELETE CASCADE,
  capability capability NOT NULL,
  PRIMARY KEY (role_id, capability)
);

CREATE TABLE admin_roles (
  admin_id uuid NOT NULL REFERENCES admin_accounts (id) ON DELETE CASCADE,
  role_id uuid NOT NULL REFERENCES roles (id) ON DELETE CASCADE,
  granted_by uuid REFERENCES admin_accounts (id),
  granted_at timestamptz NOT NULL DEFAULT now(),
  PRIMARY KEY (admin_id, role_id)
);

CREATE TABLE sessions (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  account_kind account_kind NOT NULL,
  learner_id uuid REFERENCES learner_accounts (id) ON DELETE CASCADE,
  admin_id uuid REFERENCES admin_accounts (id) ON DELETE CASCADE,
  system_id uuid REFERENCES system_accounts (id) ON DELETE CASCADE,
  created_at timestamptz NOT NULL DEFAULT now(),
  last_seen_at timestamptz NOT NULL DEFAULT now(),
  expires_at timestamptz NOT NULL,
  revoked_at timestamptz,
  user_agent text,
  client_address inet,
  CONSTRAINT sessions_expiry_after_creation CHECK (expires_at > created_at),
  CONSTRAINT sessions_one_account CHECK (
    (account_kind = 'learner' AND learner_id IS NOT NULL AND admin_id IS NULL AND system_id IS NULL)
    OR (account_kind = 'admin' AND admin_id IS NOT NULL AND learner_id IS NULL AND system_id IS NULL)
    OR (account_kind = 'system' AND system_id IS NOT NULL AND learner_id IS NULL AND admin_id IS NULL)
  )
);

CREATE INDEX sessions_live_idx ON sessions (learner_id, expires_at) WHERE revoked_at IS NULL;
CREATE INDEX sessions_live_admin_idx ON sessions (admin_id, expires_at) WHERE revoked_at IS NULL;

CREATE TABLE elevations (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  admin_id uuid NOT NULL REFERENCES admin_accounts (id) ON DELETE CASCADE,
  capability capability NOT NULL,
  granted_actor_kind account_kind NOT NULL,
  granted_learner_actor_id uuid REFERENCES learner_accounts (id),
  granted_admin_actor_id uuid REFERENCES admin_accounts (id),
  granted_system_actor_id uuid REFERENCES system_accounts (id),
  approved_actor_kind account_kind,
  approved_learner_actor_id uuid REFERENCES learner_accounts (id),
  approved_admin_actor_id uuid REFERENCES admin_accounts (id),
  approved_system_actor_id uuid REFERENCES system_accounts (id),
  granted_at timestamptz NOT NULL DEFAULT now(),
  expires_at timestamptz NOT NULL,
  consumed_at timestamptz,
  reason text NOT NULL,
  CONSTRAINT elevations_reason_present CHECK (length(btrim(reason)) > 0),
  CONSTRAINT elevations_expiry_after_grant CHECK (expires_at > granted_at),
  CONSTRAINT elevations_maximum_minutes CHECK (expires_at <= granted_at + interval '60 minutes'),
  CONSTRAINT elevations_grantor_shape CHECK (
    (granted_actor_kind = 'learner' AND granted_learner_actor_id IS NOT NULL
      AND granted_admin_actor_id IS NULL AND granted_system_actor_id IS NULL)
    OR (granted_actor_kind = 'admin' AND granted_admin_actor_id IS NOT NULL
      AND granted_learner_actor_id IS NULL AND granted_system_actor_id IS NULL)
    OR (granted_actor_kind = 'system' AND granted_system_actor_id IS NOT NULL
      AND granted_learner_actor_id IS NULL AND granted_admin_actor_id IS NULL)
  ),
  -- A null approver is the pending state, not a missing fact: all four columns stay null until one lands.
  CONSTRAINT elevations_approver_shape CHECK (
    (approved_actor_kind IS NULL AND approved_learner_actor_id IS NULL
      AND approved_admin_actor_id IS NULL AND approved_system_actor_id IS NULL)
    OR (approved_actor_kind = 'learner' AND approved_learner_actor_id IS NOT NULL
      AND approved_admin_actor_id IS NULL AND approved_system_actor_id IS NULL)
    OR (approved_actor_kind = 'admin' AND approved_admin_actor_id IS NOT NULL
      AND approved_learner_actor_id IS NULL AND approved_system_actor_id IS NULL)
    OR (approved_actor_kind = 'system' AND approved_system_actor_id IS NOT NULL
      AND approved_learner_actor_id IS NULL AND approved_admin_actor_id IS NULL)
  ),
  -- Two people, and the beneficiary is neither of them. The kind is compared beside the id because the
  -- same uuid in two account tables is two different people.
  CONSTRAINT elevations_two_people CHECK (
    approved_actor_kind IS NULL
    OR (
      NOT (approved_actor_kind = 'admin' AND approved_admin_actor_id = admin_id)
      AND NOT (
        approved_actor_kind = granted_actor_kind
        AND coalesce(approved_learner_actor_id, approved_admin_actor_id, approved_system_actor_id)
          = coalesce(granted_learner_actor_id, granted_admin_actor_id, granted_system_actor_id)
      )
    )
  ),
  CONSTRAINT elevations_grantor_not_self CHECK (
    NOT (granted_actor_kind = 'admin' AND granted_admin_actor_id = admin_id)
  )
);

CREATE INDEX elevations_live_idx ON elevations (admin_id, capability, expires_at)
  WHERE consumed_at IS NULL;
