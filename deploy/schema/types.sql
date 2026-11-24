CREATE TYPE setting_kind AS ENUM ('bool', 'int', 'text');


CREATE TYPE principal_status AS ENUM ('pending', 'active', 'suspended', 'closed');

-- An administrator is never pending: the account exists because an administrator created it (ADR 0016).
CREATE TYPE admin_status AS ENUM ('active', 'revoked');

-- Which table an account id belongs to. Two ids from two tables are otherwise indistinguishable, so
-- every row that can name an account of either kind carries this beside the id (ADR 0016).
CREATE TYPE account_kind AS ENUM ('learner', 'admin', 'system');

-- An acting account: the kind and the id together, because an id alone cannot say which table it names.
CREATE TYPE actor_identity AS (kind account_kind, id uuid);

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
  'settings:secret_write',
  'infra:read',
  'infra:write',
  'audit:read',
  'schema:apply',
  'generation:write'
);

-- What an emailed token is for. Kept separate from the principal status so one table can serve both
-- verification and a later password reset without widening an existing column.
CREATE TYPE token_purpose AS ENUM ('verify_email', 'reset_password');

CREATE TYPE task_status AS ENUM ('draft', 'verified', 'published', 'rejected');

CREATE TYPE task_verification_status AS ENUM ('pending', 'verified', 'failed');

CREATE TYPE task_checker_kind AS ENUM ('standard', 'token', 'testlib', 'interactive');

CREATE TYPE task_origin_kind AS ENUM ('original', 'imported', 'ai');

CREATE TYPE locale AS ENUM ('en', 'th');

-- What a checker can say, in the judge's own vocabulary plus the two states the judge itself produces.
CREATE TYPE verdict_status AS ENUM (
  'ok',
  'wrong_answer',
  'wrong_format',
  'partially_correct',
  'failed',
  'check_failed'
);
