CREATE TRIGGER settings_stamp_row
  BEFORE INSERT OR UPDATE ON settings
  FOR EACH ROW EXECUTE FUNCTION settings_stamp();

CREATE TRIGGER settings_audit_row
  AFTER INSERT OR UPDATE OR DELETE ON settings
  FOR EACH ROW EXECUTE FUNCTION settings_write_audit();

-- Row level: a statement trigger fires even when no row matched.
CREATE TRIGGER settings_version_bump
  AFTER INSERT OR UPDATE OR DELETE ON settings
  FOR EACH ROW EXECUTE FUNCTION settings_bump_version();

CREATE TRIGGER settings_audit_append_only
  BEFORE UPDATE OR DELETE ON settings_audit
  FOR EACH ROW EXECUTE FUNCTION reject_mutation();
