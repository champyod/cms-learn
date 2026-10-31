#pragma once

#include <sqlpp23/sqlpp23.h>

#include <cms/db/tables.hpp>

namespace cms::db {

SQLPP_CREATE_NAME_TAG(session_id);
SQLPP_CREATE_NAME_TAG(now);

inline auto settings_snapshot_query() {
  return sqlpp::select(settings.key, settings.kind, settings.valueText, settings.tier)
      .from(settings);
}

/// The counter the settings trigger bumps. Reading it is how a process that missed a notification
/// finds out, so it costs one integer query rather than a full reload.
inline auto settings_version_query() {
  return sqlpp::select(settingsMeta.version).from(settingsMeta);
}

inline auto live_session_query() {
  const auto id = sqlpp::parameter(sqlpp::text{}, session_id);
  const auto clock = sqlpp::parameter(sqlpp::timestamp{}, now);
  return sqlpp::select(sessions.principalId, sessions.expiresAt, sessions.revokedAt)
      .from(sessions)
      .where(sessions.id == id && sessions.revokedAt.is_null() &&
             sessions.expiresAt > clock);
}

}  // namespace cms::db
