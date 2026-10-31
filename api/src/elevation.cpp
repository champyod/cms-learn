#include <cms/api/elevation.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <string_view>

#include <sqlpp23/sqlpp23.h>

#include <cms/api/audit.hpp>
#include <cms/api/settings_defaults.hpp>
#include <cms/api/uuid.hpp>
#include <cms/db/tables.hpp>

namespace cms::api {
namespace {

/// The same ceiling the audit table's check constraint states. Named here so the refusal explains the
/// rule instead of forwarding a constraint violation the operator has to look up.
constexpr std::int64_t MAXIMUM_MINUTES = 60;
constexpr std::int64_t SECONDS_PER_MINUTE = 60;
constexpr std::string_view PURPOSE_POLICY_KEY = "identity.elevation_minutes";

constexpr std::string_view ACTION_GRANTED = "elevation.granted";
constexpr std::string_view ACTION_APPROVED = "elevation.approved";
constexpr std::string_view INVOKED_BY = "admin_socket";

auto now_stamp() -> cms::db::timestamp {
  return std::chrono::time_point_cast<cms::db::timestamp::duration>(
      std::chrono::system_clock::now());
}

auto stamp_after(std::int64_t seconds) -> cms::db::timestamp {
  return now_stamp() + std::chrono::seconds{seconds};
}

auto is_blank(std::string_view text) -> bool {
  return text.find_first_not_of(" \t\r\n") == std::string_view::npos;
}

/// The whole row one lookup needs, gathered so approve decides from one read rather than three.
struct ElevationRow {
  std::string principal_id;
  std::string granted_by;
  bool is_approved{false};
  bool is_consumed{false};
  cms::db::timestamp expires_at;
  bool is_found{false};
};

auto find_elevation(db::connection& database, std::string_view elevation_id) -> ElevationRow {
  auto row = ElevationRow{};
  for (const auto& record :
       database(sqlpp::select(db::elevations.principalId, db::elevations.grantedBy,
                              db::elevations.approvedBy, db::elevations.consumedAt,
                              db::elevations.expiresAt)
                    .from(db::elevations)
                    .where(db::elevations.id == std::string{elevation_id}))) {
    row.principal_id = std::string{record.principalId};
    row.granted_by = std::string{record.grantedBy};
    row.is_approved = record.approvedBy.has_value();
    row.is_consumed = record.consumedAt.has_value();
    row.expires_at = record.expiresAt;
    row.is_found = true;
  }
  return row;
}

auto kind_of(db::connection& database, std::string_view principal_id) -> std::string {
  auto kind = std::string{};
  for (const auto& row : database(sqlpp::select(db::principals.kind)
                                      .from(db::principals)
                                      .where(db::principals.id == std::string{principal_id}))) {
    kind = std::string{row.kind};
  }
  return kind;
}

auto refusal(std::string_view text) -> Result<void> {
  return std::unexpected(Error::make(ErrorCode::invalid_argument, text));
}

/// Resolves the duration a request asks for: zero means the policy default, and the ceiling is the
/// same one the audit table's own constraint states.
auto resolve_minutes(const SettingsSnapshot& settings, std::int64_t requested)
    -> Result<std::int64_t> {
  const auto minutes = requested == 0 ? setting_int(settings, PURPOSE_POLICY_KEY) : requested;
  if (minutes < 1 || minutes > MAXIMUM_MINUTES) {
    return std::unexpected(Error::make(
        ErrorCode::invalid_argument,
        "an elevation lasts between 1 and " + std::to_string(MAXIMUM_MINUTES) + " minutes"));
  }
  return minutes;
}

/// Checks that both people the request names exist, that they are different people, and that the grantor
/// is a person at all.
auto check_participants(db::connection& database, const ElevationRequest& request) -> Result<void> {
  if (request.granted_by == request.principal_id) {
    return std::unexpected(Error::make(
        ErrorCode::permission_denied,
        "nobody grants themselves an elevation; ask a second person to grant it"));
  }
  if (kind_of(database, request.principal_id).empty()) {
    return std::unexpected(
        Error::make(ErrorCode::not_found, "no such principal: " + request.principal_id));
  }
  const auto grantor_kind = kind_of(database, request.granted_by);
  if (grantor_kind.empty()) {
    return std::unexpected(
        Error::make(ErrorCode::not_found, "no such grantor: " + request.granted_by));
  }
  if (grantor_kind == "system") {
    return std::unexpected(Error::make(
        ErrorCode::invalid_argument,
        "a system principal grants nothing: it is not a person and holds every capability already"));
  }
  return {};
}

/// Whether an approver is a second person: not the beneficiary, not the grantor, and not after the box
/// has closed. The database states the first two as a constraint; naming them here gives the operator
/// a sentence instead of a constraint violation to look up.
auto check_approvable(const ElevationRow& row, std::string_view approved_by,
                      std::string_view elevation_id) -> Result<void> {
  if (!row.is_found) {
    return std::unexpected(
        Error::make(ErrorCode::not_found, "no such elevation: " + std::string{elevation_id}));
  }
  if (row.is_consumed) {
    return refusal("that elevation has already been spent");
  }
  if (row.is_approved) {
    return std::unexpected(
        Error::make(ErrorCode::conflict, "that elevation already has its second person"));
  }
  if (approved_by == row.principal_id) {
    return std::unexpected(Error::make(ErrorCode::permission_denied,
                                       "the account receiving an elevation cannot approve it"));
  }
  if (approved_by == row.granted_by) {
    return std::unexpected(Error::make(ErrorCode::permission_denied,
                                       "the account that granted an elevation cannot approve it"));
  }
  if (row.expires_at <= now_stamp()) {
    return refusal("that elevation expired before anyone approved it");
  }
  return {};
}

}  // namespace

auto grant_elevation(db::connection& database, const SettingsSnapshot& settings,
                     const ElevationRequest& request) -> Result<ElevationGrant> {
  const auto capability = capability_from_name(request.capability);
  if (!capability) {
    return std::unexpected(
        Error::make(ErrorCode::invalid_argument,
                    "unknown capability: " + request.capability +
                        "; the enumeration in the core is the only list there is"));
  }
  if (is_blank(request.reason)) {
    return std::unexpected(
        Error::make(ErrorCode::invalid_argument, "an elevation needs a reason; write why"));
  }
  const auto minutes = resolve_minutes(settings, request.minutes);
  if (!minutes) {
    return std::unexpected(minutes.error());
  }

  try {
    const auto participants = check_participants(database, request);
    if (!participants) {
      return std::unexpected(participants.error());
    }
    auto elevation_id = uuid_v4();
    database(sqlpp::insert_into(db::elevations).set(
        db::elevations.id = elevation_id,
        db::elevations.principalId = request.principal_id,
        db::elevations.capability = std::string{capability_name(*capability)},
        db::elevations.grantedBy = request.granted_by,
        db::elevations.expiresAt = stamp_after(*minutes * SECONDS_PER_MINUTE),
        db::elevations.reason = request.reason));
    record_audit(database, request.granted_by, INVOKED_BY, ACTION_GRANTED, elevation_id);
    return ElevationGrant{.id = elevation_id, .minutes = *minutes, .is_live = false};
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::conflict, error.what()));
  }
}

auto approve_elevation(db::connection& database, std::string_view elevation_id,
                       std::string_view approved_by) -> Result<void> {
  try {
    const auto row = find_elevation(database, elevation_id);
    const auto approvable = check_approvable(row, approved_by, elevation_id);
    if (!approvable) {
      return std::unexpected(approvable.error());
    }

    // Conditional on the pending state so two approvers racing cannot both succeed; the affected-row
    // count is what reports the loss.
    const auto changed = database(sqlpp::update(db::elevations)
                                     .set(db::elevations.approvedBy = std::string{approved_by})
                                     .where(db::elevations.id == std::string{elevation_id} &&
                                            db::elevations.approvedBy.is_null()));
    if (changed == 0) {
      return std::unexpected(
          Error::make(ErrorCode::conflict, "that elevation was approved by someone else first"));
    }
    record_audit(database, approved_by, INVOKED_BY, ACTION_APPROVED, elevation_id);
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }
  return {};
}

auto load_elevated_capabilities(db::connection& database, std::string_view principal_id)
    -> Result<CapabilitySet> {
  auto set = CapabilitySet{};
  try {
    for (const auto& row :
         database(sqlpp::select(db::elevations.capability)
                      .from(db::elevations)
                      .where(db::elevations.principalId == std::string{principal_id} &&
                             !db::elevations.approvedBy.is_null() &&
                             db::elevations.consumedAt.is_null() &&
                             db::elevations.expiresAt > now_stamp()))) {
      // A stored name the core does not define grants nothing, the same rule roles follow: an unknown
      // value must never widen authority.
      if (const auto known = capability_from_name(row.capability)) {
        set.grant(*known);
      }
    }
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }
  return set;
}

}  // namespace cms::api
