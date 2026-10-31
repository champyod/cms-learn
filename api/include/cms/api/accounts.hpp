#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <cms/db/connection.hpp>
#include <cms/error.hpp>
#include <cms/settings_snapshot.hpp>

namespace cms::api {

/// @brief What a completed signup produced.
struct SignupOutcome {
  std::string principal_id;
  bool mail_sent{false};
};

/// @brief Creates a pending account, issues a verification token and mails it when mail is enabled.
///
/// @param database The connection; the caller owns the transaction.
/// @param settings The snapshot the registration policy is read from.
/// @param email The address being registered.
/// @param handle The account's handle, which the schema checks for shape.
/// @return The new principal's id and whether the message went out.
///
/// Emails the token when mail is enabled; when it is not — the default — the account still exists and
/// an operator issues the link through the admin channel.
[[nodiscard]] Result<SignupOutcome> sign_up(db::connection& database, const SettingsSnapshot& settings,
                                           std::string_view email, std::string_view handle);

/// @brief Issues a fresh verification token for a pending account and returns it.
/// @return The token to deliver, which is never stored — only its hash is.
[[nodiscard]] Result<std::string> issue_verification(db::connection& database,
                                                    std::string_view principal_id);

/// @brief Consumes a verification token.
/// @return Whether the account became active. False means the address is proven and an operator must
/// approve it, because `site.registration_requires_approval` is true.
[[nodiscard]] Result<bool> verify_email(db::connection& database, const SettingsSnapshot& settings,
                                        std::string_view token);

/// @brief Moves a verified account to active.
[[nodiscard]] Result<void> approve_account(db::connection& database, std::string_view principal_id,
                                           std::string_view actor_id);

/// @brief Opens a session for an active account and returns its id, which is also the opaque cookie.
[[nodiscard]] Result<std::string> create_session(db::connection& database,
                                                 std::string_view principal_id,
                                                 std::int64_t lifetime_seconds);

}  // namespace cms::api
