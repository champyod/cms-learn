#pragma once

#include <string>
#include <string_view>

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
/// @return Success, or the reason the account could not be updated.
///
/// The password itself never reaches a log, an audit row or a response: the hash is the only value written,
/// and the audit row records the account and the action.
[[nodiscard]] Result<void> set_password(db::connection& database, const SettingsSnapshot& settings,
                                        std::string_view principal_id,
                                        std::string_view password);

/// @brief Resolves an email and password to the account they belong to.
///
/// @return The principal id, or a permission_denied error. An unknown email and a wrong password give the
/// same error, so the route cannot be used to learn which addresses have accounts; an account that exists
/// but is not active names the state it is in, because its owner has to know what to wait for.
[[nodiscard]] Result<std::string> authenticate(db::connection& database, std::string_view email,
                                               std::string_view password);

/// @brief Revokes the session named by @p session_id, ending it for every later request.
[[nodiscard]] Result<void> end_session(db::connection& database, std::string_view session_id);

}  // namespace cms::api
