GRANT USAGE ON SCHEMA public TO cmslearn_app, cmslearn_judge;

GRANT SELECT, INSERT, UPDATE ON principals, roles, sessions TO cmslearn_app;
GRANT SELECT, INSERT, DELETE ON principal_roles, role_capabilities TO cmslearn_app;
GRANT SELECT, INSERT ON elevations TO cmslearn_app;
GRANT SELECT, INSERT, UPDATE ON principal_tokens TO cmslearn_app;

GRANT SELECT, INSERT, UPDATE, DELETE ON settings TO cmslearn_app;
GRANT SELECT ON settings_meta TO cmslearn_app;
GRANT SELECT, INSERT, UPDATE ON settings_plans TO cmslearn_app;

GRANT SELECT ON settings_audit TO cmslearn_app;
GRANT SELECT, INSERT ON audit_events TO cmslearn_app;

GRANT SELECT ON settings, settings_meta TO cmslearn_judge;

GRANT USAGE, SELECT ON ALL SEQUENCES IN SCHEMA public TO cmslearn_app;
ALTER DEFAULT PRIVILEGES IN SCHEMA public GRANT USAGE, SELECT ON SEQUENCES TO cmslearn_app;

ALTER TABLE settings ENABLE ROW LEVEL SECURITY;

CREATE POLICY settings_app_all ON settings
  FOR ALL TO cmslearn_app
  USING (true) WITH CHECK (true);

CREATE POLICY settings_judge_read ON settings
  FOR SELECT TO cmslearn_judge
  USING (tier <> 'secret');

REVOKE ALL ON FUNCTION settings_write_audit() FROM PUBLIC;
REVOKE ALL ON FUNCTION settings_bump_version() FROM PUBLIC;
REVOKE ALL ON FUNCTION current_actor() FROM PUBLIC;
GRANT EXECUTE ON FUNCTION current_actor() TO cmslearn_app;
