#include <cms/api/elevation.hpp>

#include "elevation_state.hpp"

#include <cstdint>
#include <exception>
#include <string>
#include <string_view>

#include <sqlpp23/sqlpp23.h>

#include <cms/api/audit.hpp>
#include <cms/api/uuid.hpp>
#include <cms/db/tables.hpp>

namespace cms::api {

using detail::ACTION_APPROVED;
using detail::ACTION_GRANTED;
using detail::check_approvable;
using detail::check_participants;
using detail::find_elevation;
using detail::INVOKED_BY;
using detail::is_blank;
using detail::refusal;
using detail::resolve_minutes;
using detail::SECONDS_PER_MINUTE;

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
        db::elevations.adminId = request.principal_id,
        db::elevations.capability = std::string{capability_name(*capability)},
        db::elevations.grantedActorKind = request.granted_by.kind,
        db::elevations.grantedLearnerActorId = actor_id_of(request.granted_by, ACTOR_KIND_LEARNER),
        db::elevations.grantedAdminActorId = actor_id_of(request.granted_by, ACTOR_KIND_ADMIN),
        db::elevations.grantedSystemActorId = actor_id_of(request.granted_by, ACTOR_KIND_SYSTEM),
        db::elevations.expiresAt = db::stamp_after(*minutes * SECONDS_PER_MINUTE),
        db::elevations.reason = request.reason));
    record_audit(database, request.granted_by.kind, request.granted_by.id, INVOKED_BY,
                 ACTION_GRANTED, elevation_id);
    return ElevationGrant{.id = elevation_id, .minutes = *minutes, .is_live = false};
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::conflict, error.what()));
  }
}

auto approve_elevation(db::connection& database, std::string_view elevation_id,
                       const Actor& approved_by) -> Result<void> {
  if (!is_actor_kind(approved_by.kind)) {
    return refusal("an elevation would be approved by an account kind that does not exist: " +
                   approved_by.kind);
  }
  try {
    const auto row = find_elevation(database, elevation_id);
    const auto approvable = check_approvable(row, approved_by, elevation_id);
    if (!approvable) {
      return std::unexpected(approvable.error());
    }

    // Conditional on the pending state so two approvers racing cannot both succeed; the affected-row
    // count is what reports the loss.
    const auto changed =
        database(sqlpp::update(db::elevations)
                     .set(db::elevations.approvedActorKind = approved_by.kind,
                          db::elevations.approvedLearnerActorId =
                              actor_id_of(approved_by, ACTOR_KIND_LEARNER),
                          db::elevations.approvedAdminActorId =
                              actor_id_of(approved_by, ACTOR_KIND_ADMIN),
                          db::elevations.approvedSystemActorId =
                              actor_id_of(approved_by, ACTOR_KIND_SYSTEM))
                     .where(db::elevations.id == std::string{elevation_id} &&
                            db::elevations.approvedActorKind.is_null()));
    if (changed == 0) {
      return std::unexpected(
          Error::make(ErrorCode::conflict, "that elevation was approved by someone else first"));
    }
    record_audit(database, approved_by.kind, approved_by.id, INVOKED_BY, ACTION_APPROVED,
                 elevation_id);
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }
  return {};
}

auto spend_elevation(db::connection& database, std::string_view admin_id, Capability capability)
    -> Result<bool> {
  // The UPDATE carries every condition the grant needed, so an elevation is spent at most once even when
  // two requests arrive together: the second one updates no row and is told the approval was not there.
  const auto capability_name_of = std::string{capability_name(capability)};
  try {
    for (const auto& row : database(sqlpp::select(db::elevations.id)
                                        .from(db::elevations)
                                        .where(db::elevations.adminId == std::string{admin_id} &&
                                               db::elevations.capability == capability_name_of &&
                                               !db::elevations.approvedActorKind.is_null() &&
                                               db::elevations.consumedAt.is_null() &&
                                               db::elevations.expiresAt > db::now_stamp()))) {
      const auto elevation_id = std::string{row.id};
      database(sqlpp::update(db::elevations)
                   .set(db::elevations.consumedAt = db::now_stamp())
                   .where(db::elevations.id == elevation_id &&
                          db::elevations.consumedAt.is_null()));
      return true;
    }
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }
  return false;
}

auto load_elevated_capabilities(db::connection& database, std::string_view admin_id)
    -> Result<CapabilitySet> {
  auto set = CapabilitySet{};
  try {
    for (const auto& row :
         database(sqlpp::select(db::elevations.capability)
                      .from(db::elevations)
                      .where(db::elevations.adminId == std::string{admin_id} &&
                             !db::elevations.approvedActorKind.is_null() &&
                             db::elevations.consumedAt.is_null() &&
                             db::elevations.expiresAt > db::now_stamp()))) {
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
