#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <cms/capability.hpp>
#include <cms/db/connection.hpp>
#include <cms/error.hpp>
#include <cms/settings_snapshot.hpp>

namespace cms::api {

/// @brief Everything a grant records, named so a caller cannot transpose two ids of the same type.
struct ElevationRequest {
  /// The account that will hold the capability for a while.
  std::string principal_id;
  /// The capability's registered name, as the enumeration spells it.
  std::string capability;
  /// The principal asking for it. Never the beneficiary: granting yourself authority is not a grant.
  std::string granted_by;
  /// Why the elevation exists. Required, because an approver is asked to judge it.
  std::string reason;
  /// How long it should last. The database caps this at an hour.
  std::int64_t minutes{0};
};

/// @brief What a grant produced.
struct ElevationGrant {
  std::string id;
  /// Minutes actually granted after the ceiling and the policy default were applied.
  std::int64_t minutes{0};
  /// False until a second principal approves; a pending elevation grants nothing.
  bool is_live{false};
};

/// @brief Records a time-boxed capability grant that waits for a second person.
///
/// @param database The connection; the caller owns the transaction.
/// @param settings The snapshot the policy default for the duration is read from.
/// @param request The grant. The capability name must come from the enumeration, the reason must not
/// be blank, the beneficiary must be a user account, and the grantor must be someone else. A duration
/// of zero means "the policy default", which is why the snapshot is needed.
/// @return The new elevation's id and whether it is already live — never at this point.
///
/// The row is inserted pending — `approved_by` null — so this call alone changes no authority. What
/// makes it live is a different principal calling approve_elevation().
[[nodiscard]] Result<ElevationGrant> grant_elevation(db::connection& database,
                                                     const SettingsSnapshot& settings,
                                                     const ElevationRequest& request);

/// @brief Makes a pending elevation live.
///
/// @param approved_by The second principal. The database refuses the beneficiary and the grantor, so
/// two people is a fact rather than a convention this function remembers.
/// @return Success, or the reason the elevation cannot go live.
[[nodiscard]] Result<void> approve_elevation(db::connection& database, std::string_view elevation_id,
                                             std::string_view approved_by);

/// @brief The capabilities a principal holds through live elevations right now.
/// @param database The connection to read through.
/// @param principal_id The account whose transient authority is being read.
[[nodiscard]] Result<CapabilitySet> load_elevated_capabilities(db::connection& database,
                                                               std::string_view principal_id);

}  // namespace cms::api
