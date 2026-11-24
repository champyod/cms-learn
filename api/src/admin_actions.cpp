#include <cms/api/admin_actions.hpp>

#include "admin_action_state.hpp"

#include <exception>
#include <optional>
#include <string>
#include <string_view>

#include <sqlpp23/sqlpp23.h>

#include <cms/api/accounts.hpp>
#include <cms/api/audit.hpp>
#include <cms/api/auth.hpp>
#include <cms/api/authorize.hpp>
#include <cms/api/settings_defaults.hpp>
#include <cms/api/uuid.hpp>
#include <cms/capability.hpp>
#include <cms/db/tables.hpp>

namespace cms::api {
namespace {

constexpr std::string_view INVOKED_BY = "admin_action";

constexpr std::string_view STATUS_ACTIVE = "active";
constexpr std::string_view ADMIN_STATUS_REVOKED = "revoked";

constexpr std::string_view REQUIRES_EMAIL_KEY = "site.registration_requires_email";

constexpr std::string_view STATE_QUEUED = "queued";

SQLPP_CREATE_NAME_TAG(job_id);
using new_job_row = ::sqlpp::result_row_t<::sqlpp::field_spec_t<job_id_t::_sqlpp_name_tag, ::sqlpp::integral>>;

/// The identity is generated, so the id comes back from the insert rather than being chosen by the caller.
constexpr std::string_view INSERT_JOB = R"(
INSERT INTO generation_jobs (task_id, task_version, brief) VALUES ($1, $2, $3) RETURNING id AS job_id
)";

constexpr std::string_view ACTION_CREATE_LEARNER = "account.create_learner";
constexpr std::string_view ACTION_CREATE_ADMIN = "account.create_admin";
constexpr std::string_view ACTION_ISSUE_VERIFICATION = "account.issue_verification";
constexpr std::string_view ACTION_REVOKE_ADMIN = "account.revoke_admin";
constexpr std::string_view ACTION_GRANT_ROLE = "account.grant_role";
constexpr std::string_view ACTION_CREATE_GENERATION_JOB = "generation.create_job";

auto insert_learner(db::connection& database, const std::string& learner_id,
                    const CreateLearnerRequest& request) -> void {
  auto level = std::optional<std::string>{};
  if (!request.level_id.empty()) {
    level = request.level_id;
  }
  if (request.email.empty()) {
    database(sqlpp::insert_into(db::learnerAccounts).set(
        db::learnerAccounts.id = learner_id, db::learnerAccounts.handle = request.handle,
        db::learnerAccounts.status = std::string{STATUS_ACTIVE},
        db::learnerAccounts.levelId = level));
    return;
  }
  database(sqlpp::insert_into(db::learnerAccounts).set(
      db::learnerAccounts.id = learner_id, db::learnerAccounts.handle = request.handle,
      db::learnerAccounts.email = request.email,
      db::learnerAccounts.status = std::string{STATUS_ACTIVE},
      db::learnerAccounts.levelId = level));
}

auto grant_admin_role(db::connection& database, const Actor& actor, const std::string& admin_id,
                      const std::string& role_id) -> void {
  if (detail::is_admin_actor(actor)) {
    // admin_roles.granted_by references admin_accounts, so the system account cannot be named there.
    database(sqlpp::insert_into(db::adminRoles).set(
        db::adminRoles.adminId = admin_id, db::adminRoles.roleId = role_id,
        db::adminRoles.grantedBy = std::optional<std::string>{actor.id}));
    return;
  }
  database(sqlpp::insert_into(db::adminRoles).set(db::adminRoles.adminId = admin_id,
                                                  db::adminRoles.roleId = role_id));
}

}  // namespace

auto create_learner(db::connection& database, const SettingsSnapshot& settings,
                    const ActingAccount& actor, const CreateLearnerRequest& request)
    -> Result<ActionOutcome> {
  if (const auto refused = detail::unknown_actor(actor)) {
    return std::unexpected(*refused);
  }
  const auto allowed = authorize(database, actor.kind, actor.id, Capability::user_write);
  if (!allowed) {
    return std::unexpected(allowed.error());
  }
  if (request.email.empty() && setting_bool(settings, REQUIRES_EMAIL_KEY)) {
    return std::unexpected(Error::make(
        ErrorCode::invalid_argument,
        "site.registration_requires_email is on, so a new learner needs an email address"));
  }

  const auto learner_id = uuid_v4();
  try {
    insert_learner(database, learner_id, request);
    if (!request.password.empty()) {
      const auto stored =
          set_password(database, settings, learner_id, request.password, actor, INVOKED_BY);
      if (!stored) {
        return std::unexpected(stored.error());
      }
    }
    record_audit(database, actor.kind, actor.id, INVOKED_BY, ACTION_CREATE_LEARNER, learner_id);
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }
  return ActionOutcome{.subject = learner_id,
                       .detail = "created the learner account " + request.handle};
}

auto create_generation_job(db::connection& database, const ActingAccount& actor,
                           const CreateGenerationJobRequest& request) -> Result<ActionOutcome> {
  if (const auto refused = detail::unknown_actor(actor)) {
    return std::unexpected(*refused);
  }
  const auto allowed = authorize(database, actor.kind, actor.id, Capability::generation_write);
  if (!allowed) {
    return std::unexpected(allowed.error());
  }
  if (request.task_id.empty()) {
    return std::unexpected(
        Error::make(ErrorCode::invalid_argument, "a generation job needs the task it drafts"));
  }
  if (request.brief.empty()) {
    return std::unexpected(Error::make(
        ErrorCode::invalid_argument,
        "a generation job needs a brief: the leak checks compare the statement against what was asked for"));
  }
  if (request.task_version < 1) {
    return std::unexpected(Error::make(ErrorCode::invalid_argument,
                                       "a generation job needs a task version of one or more"));
  }
  try {
    auto job = std::string{};
    for (const auto& row : ::sqlpp::result_t<db::result, new_job_row>{database.query_text(
             std::string{INSERT_JOB}, {request.task_id, request.task_version, request.brief})}) {
      job = std::to_string(row.job_id);
    }
    if (job.empty()) {
      return std::unexpected(Error::make(ErrorCode::unavailable,
                                         "the job was written and its id could not be read back"));
    }
    record_audit(database, actor.kind, actor.id, INVOKED_BY, ACTION_CREATE_GENERATION_JOB, job);
    return ActionOutcome{.subject = job, .detail = "queued generation job " + job};
  } catch (const std::exception& failure) {
    return std::unexpected(Error::make(ErrorCode::unavailable, failure.what()));
  }
}

auto create_admin(db::connection& database, const SettingsSnapshot& settings,
                  const ActingAccount& actor, const CreateAdminRequest& request)
    -> Result<ActionOutcome> {
  if (const auto refused = detail::unknown_actor(actor)) {
    return std::unexpected(*refused);
  }
  const auto allowed = authorize(database, actor.kind, actor.id, Capability::user_write);
  if (!allowed) {
    return std::unexpected(allowed.error());
  }

  const auto admin_id = uuid_v4();
  try {
    for (const auto& role_id : request.role_ids) {
      if (const auto refused = detail::refused_unheld_role(database, actor, role_id)) {
        return std::unexpected(*refused);
      }
    }
    database(sqlpp::insert_into(db::adminAccounts).set(
        db::adminAccounts.id = admin_id, db::adminAccounts.handle = request.handle,
        db::adminAccounts.status = std::string{STATUS_ACTIVE}));
    for (const auto& role_id : request.role_ids) {
      grant_admin_role(database, actor, admin_id, role_id);
    }
    if (!request.password.empty()) {
      const auto stored =
          set_password(database, settings, admin_id, request.password, actor, INVOKED_BY);
      if (!stored) {
        return std::unexpected(stored.error());
      }
    }
    record_audit(database, actor.kind, actor.id, INVOKED_BY, ACTION_CREATE_ADMIN, admin_id);
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }
  return ActionOutcome{.subject = admin_id,
                       .detail = "created the administrator account " + request.handle};
}

auto approve_learner(db::connection& database, const ActingAccount& actor,
                     std::string_view learner_id) -> Result<ActionOutcome> {
  if (const auto refused = detail::unknown_actor(actor)) {
    return std::unexpected(*refused);
  }
  const auto allowed = authorize(database, actor.kind, actor.id, Capability::user_approve);
  if (!allowed) {
    return std::unexpected(allowed.error());
  }

  try {
    const auto approved = approve_account(database, learner_id, actor, INVOKED_BY);
    if (!approved) {
      return std::unexpected(approved.error());
    }
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }
  return ActionOutcome{.subject = std::string{learner_id},
                       .detail = "approved the learner account"};
}

auto grant_role(db::connection& database, const ActingAccount& actor,
                const GrantRoleRequest& request) -> Result<ActionOutcome> {
  if (const auto refused = detail::unknown_actor(actor)) {
    return std::unexpected(*refused);
  }
  const auto allowed = authorize(database, actor.kind, actor.id, Capability::user_write);
  if (!allowed) {
    return std::unexpected(allowed.error());
  }

  // One statement decides everything the grant depends on: the administrator and the role must exist, and a
  // pair that is already there writes nothing rather than failing the page with a duplicate key.
  constexpr std::string_view GRANT_SQL = R"(
INSERT INTO admin_roles (admin_id, role_id, granted_by)
SELECT $1::uuid, $2::uuid, $3
WHERE EXISTS (SELECT 1 FROM admin_accounts WHERE id = $1::uuid)
  AND EXISTS (SELECT 1 FROM roles WHERE id = $2::uuid)
ON CONFLICT DO NOTHING)";

  try {
    auto parameters = std::vector<db::bound_value>{request.admin_id, request.role_id};
    if (detail::is_admin_actor(actor)) {
      parameters.emplace_back(std::string{actor.id});
    } else {
      parameters.emplace_back(std::nullptr_t{});
    }
    const auto written =
        database.execute_text(std::string{GRANT_SQL}, std::move(parameters));
    if (written == 0) {
      const auto exists = database.execute_text(
          "SELECT 1 FROM admin_accounts WHERE id = $1::uuid", {request.admin_id});
      if (exists == 0) {
        return std::unexpected(
            Error::make(ErrorCode::not_found, "no administrator with id " + request.admin_id));
      }
      const auto role = database.execute_text("SELECT 1 FROM roles WHERE id = $1::uuid",
                                              {request.role_id});
      if (role == 0) {
        return std::unexpected(Error::make(ErrorCode::not_found, "no role with id " + request.role_id));
      }
      return ActionOutcome{.subject = request.admin_id,
                           .detail = "the administrator already holds that role"};
    }
    // A role is authority, so the grant names the administrator who gave it: without this row the trail
    // says an account holds a role and never says who decided that.
    record_audit(database, actor.kind, actor.id, INVOKED_BY, ACTION_GRANT_ROLE, request.admin_id);
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }
  return ActionOutcome{.subject = request.admin_id, .detail = "granted the role"};
}

auto issue_verification_for(db::connection& database, const SettingsSnapshot& settings,
                            const ActingAccount& actor, std::string_view learner_id)
    -> Result<ActionOutcome> {
  if (const auto refused = detail::unknown_actor(actor)) {
    return std::unexpected(*refused);
  }
  // A hand-handed setup link works with mail off, so no setting may refuse here.
  (void)settings;
  const auto allowed = authorize(database, actor.kind, actor.id, Capability::user_write);
  if (!allowed) {
    return std::unexpected(allowed.error());
  }

  const auto learner = std::string{learner_id};
  try {
    if (!detail::learner_exists(database, learner_id)) {
      return std::unexpected(Error::make(ErrorCode::not_found, "no such learner: " + learner));
    }
    if (!detail::learner_has_email(database, learner_id)) {
      return std::unexpected(Error::make(
          ErrorCode::invalid_argument,
          "that learner has no email address, so a verification token would reach nobody"));
    }
    const auto token = issue_verification(database, learner_id);
    if (!token) {
      return std::unexpected(token.error());
    }
    record_audit(database, actor.kind, actor.id, INVOKED_BY, ACTION_ISSUE_VERIFICATION, learner);
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }
  return ActionOutcome{.subject = learner, .detail = "issued a verification token"};
}

auto revoke_admin(db::connection& database, const ActingAccount& actor, std::string_view admin_id)
    -> Result<ActionOutcome> {
  if (const auto refused = detail::unknown_actor(actor)) {
    return std::unexpected(*refused);
  }
  const auto allowed = authorize(database, actor.kind, actor.id, Capability::user_write);
  if (!allowed) {
    return std::unexpected(allowed.error());
  }
  if (admin_id == actor.id) {
    return std::unexpected(Error::make(ErrorCode::permission_denied,
                                       "an administrator cannot revoke itself"));
  }

  const auto revoked_id = std::string{admin_id};
  try {
    const auto changed = database(sqlpp::update(db::adminAccounts)
                                      .set(db::adminAccounts.status = std::string{ADMIN_STATUS_REVOKED},
                                           db::adminAccounts.revokedAt = db::now_stamp())
                                      .where(db::adminAccounts.id == revoked_id));
    if (changed == 0) {
      return std::unexpected(
          Error::make(ErrorCode::not_found, "no such administrator: " + revoked_id));
    }
    record_audit(database, actor.kind, actor.id, INVOKED_BY, ACTION_REVOKE_ADMIN, revoked_id);
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }
  return ActionOutcome{.subject = revoked_id, .detail = "revoked the administrator account"};
}

}  // namespace cms::api
