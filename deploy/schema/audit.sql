CREATE TABLE audit_events (
  id bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
  occurred_at timestamptz NOT NULL DEFAULT now(),
  actor_kind account_kind NOT NULL,
  learner_actor_id uuid REFERENCES learner_accounts (id),
  admin_actor_id uuid REFERENCES admin_accounts (id),
  system_actor_id uuid REFERENCES system_accounts (id),
  invoked_by text NOT NULL CHECK (length(invoked_by) > 0),
  action text NOT NULL CHECK (action ~ '^[a-z][a-z0-9_]*(\.[a-z][a-z0-9_]*)+$'),
  subject text NOT NULL DEFAULT '',
  detail jsonb NOT NULL DEFAULT '{}'::jsonb,
  client_address inet,
  CONSTRAINT audit_events_one_actor CHECK (
    (actor_kind = 'learner' AND learner_actor_id IS NOT NULL AND admin_actor_id IS NULL AND system_actor_id IS NULL)
    OR (actor_kind = 'admin' AND admin_actor_id IS NOT NULL AND learner_actor_id IS NULL AND system_actor_id IS NULL)
    OR (actor_kind = 'system' AND system_actor_id IS NOT NULL AND learner_actor_id IS NULL AND admin_actor_id IS NULL)
  )
);

CREATE INDEX audit_events_learner_actor_idx ON audit_events (learner_actor_id, occurred_at DESC);
CREATE INDEX audit_events_admin_actor_idx ON audit_events (admin_actor_id, occurred_at DESC);

CREATE FUNCTION reject_mutation() RETURNS trigger
LANGUAGE plpgsql
SET search_path = pg_catalog, public
AS $$
BEGIN
  RAISE EXCEPTION '% is append-only; % is not permitted', TG_TABLE_NAME, TG_OP
    USING ERRCODE = 'restrict_violation';
END;
$$;

CREATE TRIGGER audit_events_append_only
  BEFORE UPDATE OR DELETE ON audit_events
  FOR EACH ROW EXECUTE FUNCTION reject_mutation();
