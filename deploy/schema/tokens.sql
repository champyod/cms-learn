-- Verification and reset tokens.
--
-- Only the hash is stored, so a database dump carries no usable link, and used_at is what makes a
-- token single use: consuming one is an UPDATE that also matches "not yet used", which is atomic
-- under the row lock.
CREATE TABLE principal_tokens (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  principal_id uuid NOT NULL REFERENCES principals (id) ON DELETE CASCADE,
  purpose token_purpose NOT NULL,
  token_hash text NOT NULL,
  created_at timestamptz NOT NULL DEFAULT now(),
  expires_at timestamptz NOT NULL,
  used_at timestamptz,
  CONSTRAINT principal_tokens_hash_shape CHECK (token_hash ~ '^[0-9a-f]{64}$'),
  CONSTRAINT principal_tokens_expiry_after_creation CHECK (expires_at > created_at)
);

CREATE UNIQUE INDEX principal_tokens_hash_idx ON principal_tokens (token_hash);

CREATE INDEX principal_tokens_live_idx ON principal_tokens (principal_id) WHERE used_at IS NULL;
