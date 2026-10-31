CREATE TABLE principals (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  kind principal_kind NOT NULL,
  handle text NOT NULL UNIQUE,
  email text UNIQUE,
  email_verified_at timestamptz,
  password_hash text,
  status principal_status NOT NULL,
  created_at timestamptz NOT NULL DEFAULT now(),
  CONSTRAINT principals_system_has_no_credential
    CHECK (kind <> 'system' OR (password_hash IS NULL AND email IS NULL)),
  CONSTRAINT principals_handle_shape CHECK (
    handle ~ '^[a-z0-9][a-z0-9_.-]{2,63}$'
    OR (kind = 'system' AND handle ~ '^system:[a-z][a-z0-9_-]{1,62}$')
  )
);

ALTER TABLE principals ADD CONSTRAINT principals_id_kind_key UNIQUE (id, kind);

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

CREATE TABLE principal_roles (
  principal_id uuid NOT NULL REFERENCES principals (id) ON DELETE CASCADE,
  role_id uuid NOT NULL REFERENCES roles (id) ON DELETE CASCADE,
  granted_by uuid REFERENCES principals (id),
  granted_at timestamptz NOT NULL DEFAULT now(),
  PRIMARY KEY (principal_id, role_id)
);

CREATE TABLE sessions (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  principal_id uuid NOT NULL,
  principal_kind principal_kind NOT NULL DEFAULT 'user',
  created_at timestamptz NOT NULL DEFAULT now(),
  last_seen_at timestamptz NOT NULL DEFAULT now(),
  expires_at timestamptz NOT NULL,
  revoked_at timestamptz,
  user_agent text,
  client_address inet,
  CONSTRAINT sessions_user_principal_only CHECK (principal_kind = 'user'),
  CONSTRAINT sessions_expiry_after_creation CHECK (expires_at > created_at),
  CONSTRAINT sessions_principal_fk
    FOREIGN KEY (principal_id, principal_kind) REFERENCES principals (id, kind) ON DELETE CASCADE
);

CREATE INDEX sessions_live_idx ON sessions (principal_id, expires_at)
  WHERE revoked_at IS NULL;

CREATE TABLE elevations (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  principal_id uuid NOT NULL REFERENCES principals (id) ON DELETE CASCADE,
  capability capability NOT NULL,
  granted_by uuid NOT NULL REFERENCES principals (id),
  approved_by uuid REFERENCES principals (id),
  granted_at timestamptz NOT NULL DEFAULT now(),
  expires_at timestamptz NOT NULL,
  consumed_at timestamptz,
  CONSTRAINT elevations_expiry_after_grant CHECK (expires_at > granted_at),
  CONSTRAINT elevations_maximum_minutes CHECK (expires_at <= granted_at + interval '60 minutes'),
  CONSTRAINT elevations_second_person CHECK (approved_by IS NULL OR approved_by <> principal_id),
  CONSTRAINT elevations_grantor_not_self CHECK (granted_by <> principal_id)
);

CREATE INDEX elevations_live_idx ON elevations (principal_id, capability, expires_at)
  WHERE consumed_at IS NULL;
