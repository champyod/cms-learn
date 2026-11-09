#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <cms/api/actor.hpp>
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
  /// The account granting it. Never the beneficiary: granting yourself authority is not a grant.
  Actor granted_by;
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
/// be blank, the beneficiary must be an administrator, and the grantor must be a different account. A
/// duration of zero means "the policy default", which is why the snapshot is needed.
/// @return The new elevation's id and whether it is already live — never at this point.
///
/// The row is inserted pending — no approved actor — so this call alone changes no authority. What
/// makes it live is a different account calling approve_elevation().
[[nodiscard]] Result<ElevationGrant> grant_elevation(db::connection& database,
                                                     const SettingsSnapshot& settings,
                                                     const ElevationRequest& request);

/// @brief Makes a pending elevation live.
///
/// @param approved_by The second account, produced by the surface that carried the request. The
/// database refuses the beneficiary and the grantor, so two people is a fact rather than a convention
/// this function remembers.
/// @return Success, or the reason the elevation cannot go live.
[[nodiscard]] Result<void> approve_elevation(db::connection& database, std::string_view elevation_id,
                                             const Actor& approved_by);

/// @brief Uses up one live elevation for @p capability, and reports whether one was there.
///
/// Reading and spending are one operation on purpose: two requests that both read the same approval
/// would otherwise both act on it, and a second person's agreement would authorise two actions.
/// @return True when an approval was spent, false when none was live, or the reason the database refused.
[[nodiscard]] Result<bool> spend_elevation(db::connection& database, std::string_view admin_id,
                                           Capability capability);

/// @brief The capabilities a principal holds through live elevations right now.
/// @param database The connection to read through.
/// @param admin_id The account whose transient authority is being read.
[[nodiscard]] Result<CapabilitySet> load_elevated_capabilities(db::connection& database,
                                                               std::string_view admin_id);

}  // namespace cms::api
