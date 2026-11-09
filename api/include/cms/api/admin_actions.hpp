#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <cms/api/actor.hpp>
#include <cms/db/connection.hpp>
#include <cms/error.hpp>
#include <cms/settings_snapshot.hpp>

namespace cms::api {

/// @brief Who is acting. A surface produces this; no request may carry it (ADR 0019).
using ActingAccount = Actor;

/// @brief What one action produced.
struct ActionOutcome {
  std::string subject;  // the account or object the action touched
  std::string detail;   // one line a caller may show
};

/// @brief The fields the administrator's Create learner page collects.
struct CreateLearnerRequest {
  std::string handle;
  std::string email;     // empty is allowed only when site.registration_requires_email is false
  std::string level_id;  // empty means no level chosen
  std::string password;  // empty means the account is created with no credential yet
};

/// @brief The fields the administrator's Create administrator page collects.
struct CreateAdminRequest {
  std::string handle;
  std::vector<std::string> role_ids;
  std::string password;  // empty means no credential yet
};

/// @brief Creates an active learner account on an administrator's vouch.
///
/// @param database The connection; the caller owns the transaction.
/// @param settings The snapshot the e-mail rule is read from.
/// @param actor The account acting, produced by the surface.
/// @param request The fields the page collected.
/// @return The new account's id and one line a caller may show.
[[nodiscard]] Result<ActionOutcome> create_learner(db::connection& database,
                                                   const SettingsSnapshot& settings,
                                                   const ActingAccount& actor,
                                                   const CreateLearnerRequest& request);

/// @brief Creates an active administrator account and grants the roles the actor holds.
///
/// @param database The connection; the caller owns the transaction.
/// @param settings The snapshot the password policy is read from.
/// @param actor The account acting, produced by the surface.
/// @param request The fields the page collected.
/// @return The new account's id and one line a caller may show.
[[nodiscard]] Result<ActionOutcome> create_admin(db::connection& database,
                                                 const SettingsSnapshot& settings,
                                                 const ActingAccount& actor,
                                                 const CreateAdminRequest& request);

/// @brief Moves a verified learner account to active.
///
/// @param database The connection; the caller owns the transaction.
/// @param actor The account acting, produced by the surface.
/// @param learner_id The learner being approved.
/// @return The approved account's id and one line a caller may show.
[[nodiscard]] Result<ActionOutcome> approve_learner(db::connection& database,
                                                    const ActingAccount& actor,
                                                    std::string_view learner_id);

/// @brief Issues a fresh verification token for a learner that has an address.
///
/// @param database The connection; the caller owns the transaction.
/// @param settings Carried for the surface; no rule in this action reads a setting.
/// @param actor The account acting, produced by the surface.
/// @param learner_id The learner the token belongs to.
/// @return The learner's id and one line a caller may show.
[[nodiscard]] Result<ActionOutcome> issue_verification_for(db::connection& database,
                                                           const SettingsSnapshot& settings,
                                                           const ActingAccount& actor,
                                                           std::string_view learner_id);

/// @brief Revokes an administrator account other than the actor itself.
///
/// @param database The connection; the caller owns the transaction.
/// @param actor The account acting, produced by the surface.
/// @param admin_id The administrator being revoked.
/// @return The revoked account's id and one line a caller may show.
[[nodiscard]] Result<ActionOutcome> revoke_admin(db::connection& database,
                                                 const ActingAccount& actor,
                                                 std::string_view admin_id);

}  // namespace cms::api
