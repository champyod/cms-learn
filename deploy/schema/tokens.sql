-- Verification and reset tokens.
--
-- Only the hash is stored, so a database dump carries no usable link, and used_at is what makes a
-- token single use: consuming one is an UPDATE that also matches "not yet used", which is atomic
-- under the row lock.
CREATE TABLE learner_tokens (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  learner_id uuid NOT NULL REFERENCES learner_accounts (id) ON DELETE CASCADE,
  purpose token_purpose NOT NULL,
  token_hash text NOT NULL,
  created_at timestamptz NOT NULL DEFAULT now(),
  expires_at timestamptz NOT NULL,
  used_at timestamptz,
  CONSTRAINT learner_tokens_hash_shape CHECK (token_hash ~ '^[0-9a-f]{64}$'),
  CONSTRAINT learner_tokens_expiry_after_creation CHECK (expires_at > created_at)
);

CREATE UNIQUE INDEX learner_tokens_hash_idx ON learner_tokens (token_hash);

CREATE INDEX learner_tokens_live_idx ON learner_tokens (learner_id) WHERE used_at IS NULL;
