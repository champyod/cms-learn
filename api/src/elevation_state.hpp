#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <sqlpp23/sqlpp23.h>

#include <cms/api/actor.hpp>
#include <cms/api/elevation.hpp>
#include <cms/api/settings_defaults.hpp>
#include <cms/db/tables.hpp>
#include <cms/error.hpp>
#include <cms/settings_snapshot.hpp>

namespace cms::api::detail {

SQLPP_CREATE_NAME_TAG(row_count);

/// The same ceiling the audit table's check constraint states. Named here so the refusal explains the
/// rule instead of forwarding a constraint violation the operator has to look up.
constexpr std::int64_t MAXIMUM_MINUTES = 60;
constexpr std::int64_t SECONDS_PER_MINUTE = 60;
constexpr std::string_view PURPOSE_POLICY_KEY = "identity.elevation_minutes";

constexpr std::string_view ACTION_GRANTED = "elevation.granted";
constexpr std::string_view ACTION_APPROVED = "elevation.approved";
constexpr std::string_view INVOKED_BY = "admin_socket";

inline auto is_blank(std::string_view text) -> bool {
  return text.find_first_not_of(" \t\r\n") == std::string_view::npos;
}

/// The whole row one approval needs, gathered so approve decides from one read rather than three.
struct ElevationRow {
  std::string admin_id;
  Actor granted_by;
  bool is_approved{false};
  bool is_consumed{false};
  cms::db::timestamp expires_at;
  bool is_found{false};
};

inline auto optional_text(const auto& value) -> std::optional<std::string> {
  return value.has_value() ? std::optional<std::string>{std::string{*value}} : std::nullopt;
}

inline auto stored_actor(std::string_view kind, const std::optional<std::string>& learner_id,
                         const std::optional<std::string>& admin_id,
                         const std::optional<std::string>& system_id) -> Actor {
  auto id = std::string{};
  if (kind == ACTOR_KIND_LEARNER) {
    id = learner_id.value_or(std::string{});
  } else if (kind == ACTOR_KIND_ADMIN) {
    id = admin_id.value_or(std::string{});
  } else if (kind == ACTOR_KIND_SYSTEM) {
    id = system_id.value_or(std::string{});
  }
  return Actor{.kind = std::string{kind}, .id = std::move(id)};
}

inline auto find_elevation(db::connection& database, std::string_view elevation_id) -> ElevationRow {
  auto row = ElevationRow{};
  for (const auto& record : database(sqlpp::select(
                                    db::elevations.adminId, db::elevations.grantedActorKind,
                                    db::elevations.grantedLearnerActorId,
                                    db::elevations.grantedAdminActorId,
                                    db::elevations.grantedSystemActorId,
                                    db::elevations.approvedActorKind, db::elevations.consumedAt,
                                    db::elevations.expiresAt)
                                    .from(db::elevations)
                                    .where(db::elevations.id == std::string{elevation_id}))) {
    row.admin_id = std::string{record.adminId};
    row.granted_by = stored_actor(record.grantedActorKind,
                                  optional_text(record.grantedLearnerActorId),
                                  optional_text(record.grantedAdminActorId),
                                  optional_text(record.grantedSystemActorId));
    row.is_approved = record.approvedActorKind.has_value();
    row.is_consumed = record.consumedAt.has_value();
    row.expires_at = record.expiresAt;
    row.is_found = true;
  }
  return row;
}

inline auto learner_exists(db::connection& database, std::string_view learner_id) -> bool {
  const auto one = sqlpp::value(1);
  auto found = false;
  for (const auto& row : database(sqlpp::select(sqlpp::count(one).as(row_count))
                                      .from(db::learnerAccounts)
                                      .where(db::learnerAccounts.id ==
                                             std::string{learner_id}))) {
    found = row.row_count > 0;
  }
  return found;
}

inline auto admin_exists(db::connection& database, std::string_view admin_id) -> bool {
  const auto one = sqlpp::value(1);
  auto found = false;
  for (const auto& row : database(sqlpp::select(sqlpp::count(one).as(row_count))
                                      .from(db::adminAccounts)
                                      .where(db::adminAccounts.id == std::string{admin_id}))) {
    found = row.row_count > 0;
  }
  return found;
}

inline auto system_exists(db::connection& database, std::string_view system_id) -> bool {
  const auto one = sqlpp::value(1);
  auto found = false;
  for (const auto& row : database(sqlpp::select(sqlpp::count(one).as(row_count))
                                      .from(db::systemAccounts)
                                      .where(db::systemAccounts.id == std::string{system_id}))) {
    found = row.row_count > 0;
  }
  return found;
}

inline auto actor_exists(db::connection& database, const Actor& actor) -> bool {
  if (actor.kind == ACTOR_KIND_LEARNER) {
    return learner_exists(database, actor.id);
  }
  if (actor.kind == ACTOR_KIND_ADMIN) {
    return admin_exists(database, actor.id);
  }
  return system_exists(database, actor.id);
}

inline auto refusal(std::string_view text) -> Result<void> {
  return std::unexpected(Error::make(ErrorCode::invalid_argument, text));
}

/// Resolves the duration a request asks for: zero means the policy default, and the ceiling is the
/// same one the audit table's own constraint states.
inline auto resolve_minutes(const SettingsSnapshot& settings, std::int64_t requested)
    -> Result<std::int64_t> {
  const auto minutes = requested == 0 ? setting_int(settings, PURPOSE_POLICY_KEY) : requested;
  if (minutes < 1 || minutes > MAXIMUM_MINUTES) {
    return std::unexpected(Error::make(
        ErrorCode::invalid_argument,
        "an elevation lasts between 1 and " + std::to_string(MAXIMUM_MINUTES) + " minutes"));
  }
  return minutes;
}

/// Checks that the beneficiary is an administrator, the grantor is a known account, and the two are
/// different people.
inline auto check_participants(db::connection& database, const ElevationRequest& request)
    -> Result<void> {
  if (!is_actor_kind(request.granted_by.kind)) {
    return refusal("an elevation would be granted by an account kind that does not exist: " +
                   request.granted_by.kind);
  }
  if (request.granted_by.kind == ACTOR_KIND_ADMIN &&
      request.granted_by.id == request.principal_id) {
    return std::unexpected(Error::make(
        ErrorCode::permission_denied,
        "nobody grants themselves an elevation; ask a second person to grant it"));
  }
  if (!admin_exists(database, request.principal_id)) {
    return std::unexpected(
        Error::make(ErrorCode::not_found, "no such administrator: " + request.principal_id));
  }
  if (!actor_exists(database, request.granted_by)) {
    return std::unexpected(
        Error::make(ErrorCode::not_found, "no such grantor: " + request.granted_by.id));
  }
  return {};
}

/// Whether an approver is a second person: not the beneficiary, not the grantor, and not after the box
/// has closed. The database states the first two as a constraint; naming them here gives the operator
/// a sentence instead of a constraint violation to look up.
inline auto check_approvable(const ElevationRow& row, const Actor& approved_by,
                             std::string_view elevation_id) -> Result<void> {
  if (!row.is_found) {
    return std::unexpected(
        Error::make(ErrorCode::not_found, "no such elevation: " + std::string{elevation_id}));
  }
  if (row.is_consumed) {
    return refusal("that elevation has already been spent");
  }
  if (row.is_approved) {
    return std::unexpected(Error::make(ErrorCode::conflict,
                                       "that elevation already has its second person"));
  }
  if (approved_by.kind == ACTOR_KIND_ADMIN && approved_by.id == row.admin_id) {
    return std::unexpected(Error::make(ErrorCode::permission_denied,
                                       "the account receiving an elevation cannot approve it"));
  }
  if (approved_by.kind == row.granted_by.kind && approved_by.id == row.granted_by.id) {
    return std::unexpected(Error::make(ErrorCode::permission_denied,
                                       "the account that granted an elevation cannot approve it"));
  }
  if (row.expires_at <= db::now_stamp()) {
    return refusal("that elevation expired before anyone approved it");
  }
  return {};
}

}  // namespace cms::api::detail
