-- A task's audience. The kind is carried in the number, not a second column: 0 is everyone, and
-- kind·2^48 + id is one learner, administrator or team. One module in the bank builds and decodes these
-- values; nothing else may compute 2^48.
--
-- No foreign key to tasks: the audience belongs to the task id across every version, while tasks is
-- keyed (id, version) and has no unique key on id alone, so a per-version foreign key would enforce a
-- rule the ADR does not state and a unique (id) would collapse versioning.
CREATE TABLE task_visibility (
  task_id text NOT NULL,
  principal_id bigint NOT NULL,
  CONSTRAINT task_visibility_pkey PRIMARY KEY (task_id, principal_id),
  CONSTRAINT task_visibility_principal_range CHECK (
    principal_id = 0
    OR (principal_id >= (1::bigint << 48) AND principal_id < (2::bigint << 48))
    OR (principal_id >= (2::bigint << 48) AND principal_id < (3::bigint << 48))
    OR (principal_id >= (3::bigint << 48) AND principal_id < (4::bigint << 48))
  )
);
