#pragma once

#include <string>
#include <string_view>

#include <cms/api/actor.hpp>
#include <cms/db/connection.hpp>
#include <cms/error.hpp>
#include <cms/settings_snapshot.hpp>

namespace cms::api {

/// @brief Whether @p password satisfies the site's declared minimum length.
///
/// @param settings The snapshot the policy is read from.
/// @param password The candidate, which is never stored or echoed by this function.
/// @return Success, or the reason the password is refused.
[[nodiscard]] Result<void> validate_password(const SettingsSnapshot& settings,
                                             std::string_view password);

/// @brief Hashes @p password and stores it on the account.
///
/// @param database The connection; the caller owns the transaction.
/// @param settings The snapshot the hashing parameters are read from.
/// @param principal_id The account the password belongs to.
/// @param password The candidate, hashed before it is stored.
/// @param actor The account the change is attributed to.
/// @param invoked_by How it was invoked — the surface, not the actor.
/// @return Success, or the reason the account could not be updated.
///
/// The password itself never reaches a log, an audit row or a response: the hash is the only value written,
/// and the audit row names the actor who made the change.
[[nodiscard]] Result<void> set_password(db::connection& database, const SettingsSnapshot& settings,
                                        std::string_view principal_id, std::string_view password,
                                        const Actor& actor, std::string_view invoked_by);

/// @brief An authenticated account: which table its id names, and the id itself.
///
/// The kind travels with the id because two ids from two tables are indistinguishable and every audit row
/// has to name its kind.
struct Account {
  std::string kind;
  std::string id;

  /// @brief True when no account was resolved, which is what an absent cookie yields.
  [[nodiscard]] auto empty() const -> bool { return kind.empty() && id.empty(); }
};

/// @brief Resolves an identifier and password to the account they belong to.
///
/// @param identifier A learner's email or an administrator's handle; the learner table is tried first.
/// @return The account, or a permission_denied error. An unknown identifier, a wrong password and an
/// inactive account give the same error, so the route cannot be used to learn which accounts exist or
/// what state they are in.
[[nodiscard]] Result<Account> authenticate(db::connection& database, std::string_view identifier,
                                           std::string_view password);

/// @brief Revokes the session named by @p session_id, ending it for every later request.
[[nodiscard]] Result<void> end_session(db::connection& database, std::string_view session_id);

}  // namespace cms::api
