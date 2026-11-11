#include "live_database.hpp"

#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>
#include <sqlpp23/sqlpp23.h>

#include <cms/api/admin_actions.hpp>
#include <cms/db/tables.hpp>
#include <cms/settings_snapshot.hpp>

namespace {

SQLPP_CREATE_NAME_TAG(row_count);

constexpr std::string_view WRITER_ID = "a0000001-1111-4111-8111-000000000001";
constexpr std::string_view WRITER_HANDLE = "actions_writer";
constexpr std::string_view WRITER_ROLE_ID = "b0000001-1111-4111-8111-000000000001";
constexpr std::string_view APPROVER_ID = "a0000002-1111-4111-8111-000000000002";
constexpr std::string_view APPROVER_HANDLE = "actions_approver";
constexpr std::string_view APPROVER_ROLE_ID = "b0000002-1111-4111-8111-000000000002";
constexpr std::string_view TARGET_ID = "a0000004-1111-4111-8111-000000000004";
constexpr std::string_view TARGET_HANDLE = "actions_revoke_target";
constexpr std::string_view SYSTEM_ID = "c0000001-1111-4111-8111-000000000001";
constexpr std::string_view PENDING_LEARNER_ID = "d0000001-1111-4111-8111-000000000001";
constexpr std::string_view PENDING_LEARNER_HANDLE = "actions_pending";
constexpr std::string_view PENDING_LEARNER_EMAIL = "actions-pending@example.org";
constexpr std::string_view SYSTEM_PENDING_ID = "d0000004-1111-4111-8111-000000000004";
constexpr std::string_view SYSTEM_PENDING_HANDLE = "actions_system_pending";
constexpr std::string_view SYSTEM_PENDING_EMAIL = "actions-system-pending@example.org";
constexpr std::string_view EMAILED_LEARNER_ID = "d0000002-1111-4111-8111-000000000002";
constexpr std::string_view EMAILED_LEARNER_HANDLE = "actions_emailed";
constexpr std::string_view EMAILED_LEARNER_EMAIL = "actions-emailed@example.org";
constexpr std::string_view ADDRESSLESS_LEARNER_ID = "d0000003-1111-4111-8111-000000000003";
constexpr std::string_view ADDRESSLESS_LEARNER_HANDLE = "actions_addressless";
constexpr std::string_view INVOKED_BY = "admin_action";

auto skip_without_database() -> void {
  if (std::getenv("CMSLEAN_TEST_DSN") == nullptr) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
}

auto live_settings() -> cms::SettingsSnapshot {
  auto settings = cms::SettingsSnapshot{1, {}};
  settings.values.emplace("site.registration_requires_email", true);
  return settings;
}

auto acting(std::string_view kind, std::string_view id) -> cms::api::ActingAccount {
  return cms::api::ActingAccount{.kind = std::string{kind}, .id = std::string{id}};
}

auto count_rows(cms::db::connection& database, const auto& statement) -> std::int64_t {
  auto count = std::int64_t{0};
  for (const auto& row : database(statement)) {
    count = static_cast<std::int64_t>(row.row_count);
  }
  return count;
}

auto audit_rows(cms::db::connection& database, std::string_view subject, std::string_view action,
                std::string_view invoked_by) -> std::int64_t {
  const auto one = sqlpp::value(1);
  return count_rows(database, sqlpp::select(sqlpp::count(one).as(row_count))
                                  .from(cms::db::auditEvents)
                                  .where(cms::db::auditEvents.subject == std::string{subject} &&
                                         cms::db::auditEvents.action == std::string{action} &&
                                         cms::db::auditEvents.invokedBy ==
                                             std::string{invoked_by}));
}

auto seed_capability(cms::db::connection& database, std::string_view admin_id,
                     std::string_view handle, std::string_view role_id,
                     std::string_view role_name, std::string_view capability) -> void {
  database(sqlpp::insert_into(cms::db::adminAccounts).set(
      cms::db::adminAccounts.id = std::string{admin_id},
      cms::db::adminAccounts.handle = std::string{handle},
      cms::db::adminAccounts.status = std::string{"active"}));
  database(sqlpp::insert_into(cms::db::roles).set(
      cms::db::roles.id = std::string{role_id}, cms::db::roles.name = std::string{role_name}));
  database(sqlpp::insert_into(cms::db::roleCapabilities).set(
      cms::db::roleCapabilities.roleId = std::string{role_id},
      cms::db::roleCapabilities.capability = std::string{capability}));
  database(sqlpp::insert_into(cms::db::adminRoles).set(
      cms::db::adminRoles.adminId = std::string{admin_id},
      cms::db::adminRoles.roleId = std::string{role_id}));
}

auto prepare_writer(cms::db::connection& database) -> void {
  const auto one = sqlpp::value(1);
  const auto seeded = count_rows(database, sqlpp::select(sqlpp::count(one).as(row_count))
                                               .from(cms::db::adminAccounts)
                                               .where(cms::db::adminAccounts.id ==
                                                      std::string{WRITER_ID})) > 0;
  if (!seeded) {
    seed_capability(database, WRITER_ID, WRITER_HANDLE, WRITER_ROLE_ID, "actions_writer_role",
                    "user:write");
  }
}

/// The approver exists once, whichever case asks for it first: the fixture is shared by two cases and an
/// unconditional insert would fail the second one with a duplicate key instead of a refusal it tests.
auto prepare_approver(cms::db::connection& database) -> void {
  const auto one = sqlpp::value(1);
  const auto seeded = count_rows(database, sqlpp::select(sqlpp::count(one).as(row_count))
                                               .from(cms::db::adminAccounts)
                                               .where(cms::db::adminAccounts.id ==
                                                      std::string{APPROVER_ID})) > 0;
  if (!seeded) {
    seed_capability(database, APPROVER_ID, APPROVER_HANDLE, APPROVER_ROLE_ID,
                    "actions_approver_role", "user:approve");
  }
}

auto seed_pending_learner(cms::db::connection& database, std::string_view learner_id,
                          std::string_view handle, std::string_view email) -> void {
  database(sqlpp::insert_into(cms::db::learnerAccounts).set(
      cms::db::learnerAccounts.id = std::string{learner_id},
      cms::db::learnerAccounts.handle = std::string{handle},
      cms::db::learnerAccounts.email = std::string{email},
      cms::db::learnerAccounts.emailVerifiedAt = cms::db::now_stamp(),
      cms::db::learnerAccounts.status = std::string{"pending"}));
}

}  // namespace

TEST_CASE("an approval needs user:approve and the system may give it",
          "[admin_actions][live]") {
  skip_without_database();
  auto database = cms::test::live_database();
  prepare_writer(database);
  prepare_approver(database);
  database(sqlpp::insert_into(cms::db::systemAccounts).set(
      cms::db::systemAccounts.id = std::string{SYSTEM_ID},
      cms::db::systemAccounts.handle = std::string{"system:actions"}));
  seed_pending_learner(database, PENDING_LEARNER_ID, PENDING_LEARNER_HANDLE,
                       PENDING_LEARNER_EMAIL);
  seed_pending_learner(database, SYSTEM_PENDING_ID, SYSTEM_PENDING_HANDLE, SYSTEM_PENDING_EMAIL);

  const auto refused =
      cms::api::approve_learner(database, acting("admin", WRITER_ID), PENDING_LEARNER_ID);
  REQUIRE(!refused.has_value());
  CHECK(refused.error().code == cms::ErrorCode::permission_denied);
  CHECK(refused.error().message.find("user:approve") != std::string::npos);

  const auto approved =
      cms::api::approve_learner(database, acting("admin", APPROVER_ID), PENDING_LEARNER_ID);
  REQUIRE(approved.has_value());
  CHECK(audit_rows(database, PENDING_LEARNER_ID, "account.approve", INVOKED_BY) == 1);

  const auto by_system =
      cms::api::approve_learner(database, acting("system", SYSTEM_ID), SYSTEM_PENDING_ID);
  REQUIRE(by_system.has_value());
  CHECK(audit_rows(database, SYSTEM_PENDING_ID, "account.approve", INVOKED_BY) == 1);

  const auto one = sqlpp::value(1);
  CHECK(count_rows(database, sqlpp::select(sqlpp::count(one).as(row_count))
                                 .from(cms::db::learnerAccounts)
                                 .where(cms::db::learnerAccounts.id ==
                                            std::string{SYSTEM_PENDING_ID} &&
                                        cms::db::learnerAccounts.status ==
                                            std::string{"active"})) == 1);
  CHECK(count_rows(database, sqlpp::select(sqlpp::count(one).as(row_count))
                                 .from(cms::db::auditEvents)
                                 .where(cms::db::auditEvents.subject ==
                                            std::string{SYSTEM_PENDING_ID} &&
                                        cms::db::auditEvents.actorKind == std::string{"system"} &&
                                        cms::db::auditEvents.systemActorId ==
                                            std::string{SYSTEM_ID})) == 1);
}

TEST_CASE("a verification token needs a learner with an address",
          "[admin_actions][live]") {
  skip_without_database();
  auto database = cms::test::live_database();
  prepare_writer(database);
  database(sqlpp::insert_into(cms::db::learnerAccounts).set(
      cms::db::learnerAccounts.id = std::string{EMAILED_LEARNER_ID},
      cms::db::learnerAccounts.handle = std::string{EMAILED_LEARNER_HANDLE},
      cms::db::learnerAccounts.email = std::string{EMAILED_LEARNER_EMAIL},
      cms::db::learnerAccounts.status = std::string{"active"}));
  database(sqlpp::insert_into(cms::db::learnerAccounts).set(
      cms::db::learnerAccounts.id = std::string{ADDRESSLESS_LEARNER_ID},
      cms::db::learnerAccounts.handle = std::string{ADDRESSLESS_LEARNER_HANDLE},
      cms::db::learnerAccounts.status = std::string{"active"}));

  const auto issued = cms::api::issue_verification_for(
      database, live_settings(), acting("admin", WRITER_ID), EMAILED_LEARNER_ID);
  REQUIRE(issued.has_value());
  const auto one = sqlpp::value(1);
  CHECK(count_rows(database, sqlpp::select(sqlpp::count(one).as(row_count))
                                 .from(cms::db::learnerTokens)
                                 .where(cms::db::learnerTokens.learnerId ==
                                        std::string{EMAILED_LEARNER_ID})) == 1);

  const auto refused = cms::api::issue_verification_for(
      database, live_settings(), acting("admin", WRITER_ID), ADDRESSLESS_LEARNER_ID);
  REQUIRE(!refused.has_value());
  CHECK(refused.error().code == cms::ErrorCode::invalid_argument);
  CHECK(refused.error().message.find("email") != std::string::npos);
  CHECK(count_rows(database, sqlpp::select(sqlpp::count(one).as(row_count))
                                 .from(cms::db::learnerTokens)
                                 .where(cms::db::learnerTokens.learnerId ==
                                        std::string{ADDRESSLESS_LEARNER_ID})) == 0);
  CHECK(audit_rows(database, EMAILED_LEARNER_ID, "account.issue_verification", INVOKED_BY) == 1);
}

TEST_CASE("an administrator cannot revoke itself and can revoke another",
          "[admin_actions][live]") {
  skip_without_database();
  auto database = cms::test::live_database();
  prepare_writer(database);
  database(sqlpp::insert_into(cms::db::adminAccounts).set(
      cms::db::adminAccounts.id = std::string{TARGET_ID},
      cms::db::adminAccounts.handle = std::string{TARGET_HANDLE},
      cms::db::adminAccounts.status = std::string{"active"}));

  const auto self = cms::api::revoke_admin(database, acting("admin", WRITER_ID), WRITER_ID);
  REQUIRE(!self.has_value());
  CHECK(self.error().code == cms::ErrorCode::permission_denied);

  const auto revoked = cms::api::revoke_admin(database, acting("admin", WRITER_ID), TARGET_ID);
  REQUIRE(revoked.has_value());
  const auto one = sqlpp::value(1);
  CHECK(count_rows(database, sqlpp::select(sqlpp::count(one).as(row_count))
                                 .from(cms::db::adminAccounts)
                                 .where(cms::db::adminAccounts.id == std::string{TARGET_ID} &&
                                        cms::db::adminAccounts.status == std::string{"revoked"} &&
                                        cms::db::adminAccounts.revokedAt.is_not_null())) == 1);
  CHECK(audit_rows(database, TARGET_ID, "account.revoke_admin", INVOKED_BY) == 1);
}

TEST_CASE("a role is granted only by an account that holds user:write", "[admin_actions][live]") {
  skip_without_database();
  auto database = cms::test::live_database();
  prepare_writer(database);
  prepare_approver(database);

  const auto target = std::string{"a0000009-1111-4111-8111-000000000009"};
  database.execute_text("DELETE FROM admin_accounts WHERE id = $1::uuid", {target});
  database(sqlpp::insert_into(cms::db::adminAccounts).set(
      cms::db::adminAccounts.id = target,
      cms::db::adminAccounts.handle = std::string{"actions_grant_target"},
      cms::db::adminAccounts.status = std::string{"active"}));

  // The approver holds user:approve and not user:write, so the refusal names the capability it lacks —
  // the message an administrator needs is which authority is missing, not which role was attempted.
  const auto refused =
      cms::api::grant_role(database, acting("admin", APPROVER_ID), {.admin_id = target,
                                                                  .role_id = std::string{WRITER_ROLE_ID}});
  REQUIRE(!refused.has_value());
  CHECK(refused.error().code == cms::ErrorCode::permission_denied);
  CHECK(refused.error().message.find("user:write") != std::string::npos);

  const auto granted =
      cms::api::grant_role(database, acting("admin", WRITER_ID), {.admin_id = target,
                                                                  .role_id = std::string{WRITER_ROLE_ID}});
  REQUIRE(granted.has_value());
  CHECK(granted->subject == target);

  const auto one = sqlpp::value(1);
  CHECK(count_rows(database, sqlpp::select(sqlpp::count(one).as(row_count))
                                 .from(cms::db::adminRoles)
                                 .where(cms::db::adminRoles.adminId == target &&
                                        cms::db::adminRoles.roleId ==
                                            std::string{WRITER_ROLE_ID})) == 1);

  // Granting a role the target already holds writes nothing and says so, rather than failing the page.
  const auto again =
      cms::api::grant_role(database, acting("admin", WRITER_ID), {.admin_id = target,
                                                                  .role_id = std::string{WRITER_ROLE_ID}});
  REQUIRE(again.has_value());
  CHECK(again->detail.find("already") != std::string::npos);
}
