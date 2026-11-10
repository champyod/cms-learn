#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <cms/api/actor.hpp>
#include <cms/db/connection.hpp>
#include <cms/error.hpp>
#include <cms/settings_snapshot.hpp>

namespace cms::api {

/// The handle of the one system account bootstrap creates; the socket resolves its caller by it.
constexpr std::string_view BOOTSTRAP_SYSTEM_HANDLE = "system:bootstrap";

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

/// @brief Moves a verified account to active and records who approved it.
///
/// @param database The connection; the caller owns the transaction.
/// @param learner_id The account being approved.
/// @param actor The account the approval is attributed to.
/// @param invoked_by How it was invoked — the surface, not the actor.
[[nodiscard]] Result<void> approve_account(db::connection& database, std::string_view learner_id,
                                           const Actor& actor, std::string_view invoked_by);

/// @brief Opens a session for an active account and returns its id, which is also the opaque cookie.
///
/// @param account_kind "learner", "admin" or "system"; the session row carries this beside exactly one
/// id column, and the table's own check refuses a pair that does not match.
/// @param account_id The account the session belongs to.
[[nodiscard]] Result<std::string> create_session(db::connection& database, std::string_view account_kind,
                                                 std::string_view account_id,
                                                 std::int64_t lifetime_seconds);

/// @brief The refusal `bootstrap` gives when the database already holds an administrator.
///
/// A populated database is the steady state, not a failure: a service start calls `ensure_first_run` and
/// never sees this message. It is what the once-only form refuses with, and what the live case asserts.
inline constexpr std::string_view BOOTSTRAP_EXISTS =
    "this database already holds administrators; bootstrap runs once";

/// @brief The handle the first administrator is created with, taken from the operator's address.
///
/// @param admin_email The address the host file names (`CMSLEARN_BOOTSTRAP_ADMIN`).
/// @return The address's local part, lowercased and reduced to the characters a handle may carry, or an
/// error naming the fact when the result is not a legal handle.
///
/// The derivation exists so a new operator-facing key is not needed for the same fact: the host file
/// already requires an address, and its local part is the name a person would have chosen.
[[nodiscard]] auto bootstrap_handle(std::string_view admin_email) -> Result<std::string>;

/// @brief Ensures the two facts a first start needs, each on its own.
///
/// @param database The connection.
/// @param admin_handle The handle the first administrator is created with; `bootstrap_handle` derives it
/// from the operator's address, and the caller validates it whether or not it is used.
/// @return True when this start created the administrator, false when the database already held one — in
/// which case nothing is written and nothing is logged.
///
/// The two facts are independent. A database can hold an administrator and no system identity, and the
/// control socket refuses to start without the identity, so ensuring the identity is never conditional on
/// the administrator. The identity that already exists is read, never duplicated.
[[nodiscard]] auto ensure_first_run(db::connection& database, std::string_view admin_handle)
    -> Result<bool>;

/// @brief Creates the first system principal and the first administrator, exactly once.
///
/// @param database The connection; the caller owns the transaction.
/// @param admin_email The first administrator's address. It is not stored: an administrator carries no
/// email column (ADR 0016), and this parameter stays so a caller states the fact it bootstrapped from.
/// @param admin_handle The first administrator's handle; `bootstrap_handle` derives the one the service uses.
/// @return The administrator's id, or `BOOTSTRAP_EXISTS` as a conflict when the database already holds an
/// administrator. A system identity that already exists is reused, never duplicated.
///
/// There is no empty-database branch: the function refuses when the database is not empty rather than
/// choosing behaviour from a count, so running it twice is an error instead of a silent second seed.
/// The service does not call this: a start uses `ensure_first_run`, for which a populated database is the
/// steady state rather than a refusal. This form is what the live case states the rule with.
[[nodiscard]] Result<std::string> bootstrap(db::connection& database, std::string_view admin_email,
                                           std::string_view admin_handle);

}  // namespace cms::api
