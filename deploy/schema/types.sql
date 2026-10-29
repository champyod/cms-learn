CREATE TYPE setting_kind AS ENUM ('bool', 'int', 'text');

CREATE TYPE principal_kind AS ENUM ('user', 'system');

CREATE TYPE principal_status AS ENUM ('pending', 'active', 'suspended', 'closed');

CREATE TYPE audit_operation AS ENUM ('insert', 'update', 'delete');

-- Order is load-bearing: entry N must answer to bit N in cms::Capability.
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
