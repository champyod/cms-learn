#include "live_database.hpp"

#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>
#include <sqlpp23/sqlpp23.h>

#include <cms/api/admin_actions.hpp>
#include <cms/api/authorize.hpp>
#include <cms/capability.hpp>
#include <cms/db/tables.hpp>
#include <cms/settings_snapshot.hpp>

namespace {

SQLPP_CREATE_NAME_TAG(row_count);
constexpr std::string_view WRITER_ID = "a0000001-1111-4111-8111-000000000001";
constexpr std::string_view WRITER_HANDLE = "actions_writer";
constexpr std::string_view WRITER_ROLE_ID = "b0000001-1111-4111-8111-000000000001";
constexpr std::string_view READER_ID = "a0000003-1111-4111-8111-000000000003";
constexpr std::string_view READER_HANDLE = "actions_reader";
constexpr std::string_view READER_ROLE_ID = "b0000003-1111-4111-8111-000000000003";
constexpr std::string_view GRANTED_ROLE_ID = "b0000004-1111-4111-8111-000000000004";
constexpr std::string_view UNHELD_ROLE_ID = "b0000005-1111-4111-8111-000000000005";
constexpr std::string_view SYSTEM_ID = "c0000001-1111-4111-8111-000000000001";
constexpr std::string_view INVOKED_BY = "admin_action";
auto skip_without_database() -> void {
  if (std::getenv("CMSLEAN_TEST_DSN") == nullptr) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
}
auto live_settings(bool requires_email) -> cms::SettingsSnapshot {
  auto settings = cms::SettingsSnapshot{1, {}};
  settings.values.emplace("site.registration_requires_email", requires_email);
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
auto subject_audit_rows(cms::db::connection& database, std::string_view subject) -> std::int64_t {
  const auto one = sqlpp::value(1);
  return count_rows(database, sqlpp::select(sqlpp::count(one).as(row_count))
                                  .from(cms::db::auditEvents)
                                  .where(cms::db::auditEvents.subject == std::string{subject}));
}
auto admin_actor_rows(cms::db::connection& database, std::string_view subject,
                      std::string_view admin_id) -> std::int64_t {
  const auto one = sqlpp::value(1);
  return count_rows(database,
                    sqlpp::select(sqlpp::count(one).as(row_count))
                        .from(cms::db::auditEvents)
                        .where(cms::db::auditEvents.subject == std::string{subject} &&
                               cms::db::auditEvents.actorKind == std::string{"admin"} &&
                               cms::db::auditEvents.adminActorId == std::string{admin_id}));
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

}  // namespace

TEST_CASE("an administrator creates an active learner", "[admin_actions][live]") {
  skip_without_database();
  auto database = cms::test::live_database();
  prepare_writer(database);

  const auto request = cms::api::CreateLearnerRequest{.handle = "actions_new_learner",
                                                      .email = "actions-new@example.org",
                                                      .level_id = std::string{},
                                                      .password = std::string{"actions-password-1"}};
  const auto created =
      cms::api::create_learner(database, live_settings(true), acting("admin", WRITER_ID), request);
  REQUIRE(created.has_value());

  const auto one = sqlpp::value(1);
  CHECK(count_rows(database, sqlpp::select(sqlpp::count(one).as(row_count))
                                 .from(cms::db::learnerAccounts)
                                 .where(cms::db::learnerAccounts.id == created->subject &&
                                        cms::db::learnerAccounts.status == std::string{"active"} &&
                                        cms::db::learnerAccounts.passwordHash.is_not_null() &&
                                        cms::db::learnerAccounts.levelId.is_null())) == 1);
  CHECK(subject_audit_rows(database, created->subject) == 2);
  CHECK(audit_rows(database, created->subject, "account.create_learner", INVOKED_BY) == 1);
  CHECK(audit_rows(database, created->subject, "account.password", INVOKED_BY) == 1);
  CHECK(admin_actor_rows(database, created->subject, WRITER_ID) == 2);
}

TEST_CASE("the email switch decides a learner without an address", "[admin_actions][live]") {
  skip_without_database();
  auto database = cms::test::live_database();
  prepare_writer(database);

  const auto request = cms::api::CreateLearnerRequest{.handle = "actions_no_address",
                                                      .email = std::string{},
                                                      .level_id = std::string{},
                                                      .password = std::string{}};
  const auto refused =
      cms::api::create_learner(database, live_settings(true), acting("admin", WRITER_ID), request);
  REQUIRE(!refused.has_value());
  CHECK(refused.error().code == cms::ErrorCode::invalid_argument);

  const auto allowed =
      cms::api::create_learner(database, live_settings(false), acting("admin", WRITER_ID), request);
  REQUIRE(allowed.has_value());
  const auto one = sqlpp::value(1);
  CHECK(count_rows(database, sqlpp::select(sqlpp::count(one).as(row_count))
                                 .from(cms::db::learnerAccounts)
                                 .where(cms::db::learnerAccounts.id == allowed->subject &&
                                        cms::db::learnerAccounts.email.is_null())) == 1);
}

TEST_CASE("an action refuses an unknown kind and missing capability", "[admin_actions][live]") {
  skip_without_database();
  auto database = cms::test::live_database();
  seed_capability(database, READER_ID, READER_HANDLE, READER_ROLE_ID, "actions_reader_role",
                  "user:read");

  const auto request = cms::api::CreateLearnerRequest{.handle = "actions_refused",
                                                      .email = "actions-refused@example.org",
                                                      .level_id = std::string{},
                                                      .password = std::string{}};
  const auto unknown_kind = cms::api::create_learner(
      database, live_settings(true), acting("learner", READER_ID), request);
  REQUIRE(!unknown_kind.has_value());
  CHECK(unknown_kind.error().code == cms::ErrorCode::invalid_argument);
  CHECK(unknown_kind.error().message.find("learner") != std::string::npos);
  CHECK(cms::api::authorize(database, "admin", READER_ID, cms::Capability::user_read).has_value());

  const auto refused =
      cms::api::create_learner(database, live_settings(true), acting("admin", READER_ID), request);
  REQUIRE(!refused.has_value());
  CHECK(refused.error().code == cms::ErrorCode::permission_denied);
  CHECK(refused.error().message.find("user:write") != std::string::npos);
}

TEST_CASE("the system account creates an administrator", "[admin_actions][live]") {
  skip_without_database();
  auto database = cms::test::live_database();
  database(sqlpp::insert_into(cms::db::systemAccounts).set(
      cms::db::systemAccounts.id = std::string{SYSTEM_ID},
      cms::db::systemAccounts.handle = std::string{"system:actions"}));
  database(sqlpp::insert_into(cms::db::roles).set(
      cms::db::roles.id = std::string{GRANTED_ROLE_ID},
      cms::db::roles.name = std::string{"actions_granted_role"}));

  const auto request = cms::api::CreateAdminRequest{.handle = "actions_created_admin",
                                                    .role_ids = {std::string{GRANTED_ROLE_ID}},
                                                    .password = std::string{}};
  const auto created =
      cms::api::create_admin(database, live_settings(true), acting("system", SYSTEM_ID), request);
  REQUIRE(created.has_value());

  const auto one = sqlpp::value(1);
  CHECK(count_rows(database, sqlpp::select(sqlpp::count(one).as(row_count))
                                 .from(cms::db::adminAccounts)
                                 .where(cms::db::adminAccounts.id == created->subject &&
                                        cms::db::adminAccounts.status ==
                                            std::string{"active"})) == 1);
  CHECK(count_rows(database, sqlpp::select(sqlpp::count(one).as(row_count))
                                 .from(cms::db::adminRoles)
                                 .where(cms::db::adminRoles.adminId == created->subject &&
                                        cms::db::adminRoles.grantedBy.is_null())) == 1);
  CHECK(audit_rows(database, created->subject, "account.create_admin", INVOKED_BY) == 1);
}

TEST_CASE("an administrator cannot grant a role it does not hold", "[admin_actions][live]") {
  skip_without_database();
  auto database = cms::test::live_database();
  prepare_writer(database);
  database(sqlpp::insert_into(cms::db::roles).set(
      cms::db::roles.id = std::string{UNHELD_ROLE_ID},
      cms::db::roles.name = std::string{"actions_unheld_role"}));

  const auto request = cms::api::CreateAdminRequest{.handle = "actions_not_created",
                                                    .role_ids = {std::string{UNHELD_ROLE_ID}},
                                                    .password = std::string{}};
  const auto refused =
      cms::api::create_admin(database, live_settings(true), acting("admin", WRITER_ID), request);
  REQUIRE(!refused.has_value());
  CHECK(refused.error().code == cms::ErrorCode::permission_denied);

  const auto one = sqlpp::value(1);
  CHECK(count_rows(database, sqlpp::select(sqlpp::count(one).as(row_count))
                                 .from(cms::db::adminAccounts)
                                 .where(cms::db::adminAccounts.handle ==
                                        std::string{"actions_not_created"})) == 0);
}

TEST_CASE("a create with a password records two rows", "[admin_actions][live]") {
  skip_without_database();
  auto database = cms::test::live_database();
  prepare_writer(database);

  const auto request = cms::api::CreateAdminRequest{.handle = "actions_password_admin",
                                                    .role_ids = {},
                                                    .password =
                                                        std::string{"actions-password-1"}};
  const auto created =
      cms::api::create_admin(database, live_settings(true), acting("admin", WRITER_ID), request);
  REQUIRE(created.has_value());
  CHECK(subject_audit_rows(database, created->subject) == 2);
  CHECK(audit_rows(database, created->subject, "account.create_admin", INVOKED_BY) == 1);
  CHECK(audit_rows(database, created->subject, "account.password", INVOKED_BY) == 1);
  CHECK(admin_actor_rows(database, created->subject, WRITER_ID) == 2);
}
