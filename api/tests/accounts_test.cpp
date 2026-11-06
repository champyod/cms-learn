#include "live_database.hpp"

#include <cms/api/accounts.hpp>
#include <cms/api/authorize.hpp>
#include <cms/api/settings_defaults.hpp>
#include <cms/db/connection.hpp>
#include <cms/db/tables.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <memory>
#include <string>

#include <drogon/orm/DbClient.h>
#include <sqlpp23/sqlpp23.h>

namespace {

constexpr std::int64_t SESSION_LIFETIME_SECONDS = 3600;

auto registration_settings(bool requires_approval) -> cms::SettingsSnapshot {
  auto snapshot = cms::SettingsSnapshot{1, {}};
  snapshot.values.emplace("site.registration_open", true);
  snapshot.values.emplace("site.registration_requires_approval", requires_approval);
  snapshot.values.emplace("site.registration_domain_allowlist", "example.org");
  return snapshot;
}

}  // namespace

TEST_CASE("a signup is verified, approved, and only then opens a session", "[accounts][live]") {
  if (std::getenv("CMSLEAN_TEST_DSN") == nullptr) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }

  auto database = cms::test::live_database();
  const auto settings = registration_settings(true);

  const auto signup = cms::api::sign_up(database, settings, "student@example.org", "student_one");
  REQUIRE(signup.has_value());
  CHECK(signup->mail_sent == false);

  const auto token = cms::api::issue_verification(database, signup->principal_id);
  REQUIRE(token.has_value());

  const auto verified = cms::api::verify_email(database, settings, *token);
  REQUIRE(verified.has_value());
  CHECK(*verified == false);

  const auto premature = cms::api::create_session(database, "learner", signup->principal_id,
                                                  SESSION_LIFETIME_SECONDS);
  CHECK(!premature.has_value());

  const auto approved =
      cms::api::approve_account(database, signup->principal_id, signup->principal_id);
  REQUIRE(approved.has_value());

  const auto session = cms::api::create_session(database, "learner", signup->principal_id,
                                                SESSION_LIFETIME_SECONDS);
  REQUIRE(session.has_value());
  CHECK(session->size() == 36);

  const auto replayed = cms::api::verify_email(database, settings, *token);
  CHECK(!replayed.has_value());
}

TEST_CASE("an address outside the allowlist is refused", "[accounts][live]") {
  if (std::getenv("CMSLEAN_TEST_DSN") == nullptr) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }

  auto database = cms::test::live_database();
  const auto settings = registration_settings(false);

  const auto refused = cms::api::sign_up(database, settings, "student@elsewhere.test", "outsider");
  REQUIRE(!refused.has_value());
  CHECK(refused.error().code == cms::ErrorCode::permission_denied);
}

TEST_CASE("a closed registration refuses everyone", "[accounts][live]") {
  if (std::getenv("CMSLEAN_TEST_DSN") == nullptr) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }

  auto database = cms::test::live_database();
  auto settings = registration_settings(false);
  settings.values.insert_or_assign("site.registration_open", false);

  const auto refused = cms::api::sign_up(database, settings, "student@example.org", "closed_door");
  REQUIRE(!refused.has_value());
  CHECK(refused.error().code == cms::ErrorCode::permission_denied);
}

TEST_CASE("a session carries the kind of account it was opened for", "[accounts][live]") {
  if (std::getenv("CMSLEAN_TEST_DSN") == nullptr) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }

  auto database = cms::test::live_database();
  const auto system_id = std::string{"33333333-4444-4555-8666-777777777777"};

  database(sqlpp::insert_into(cms::db::systemAccounts).set(
      cms::db::systemAccounts.id = system_id,
      cms::db::systemAccounts.handle = std::string{"system:probe"}));

  const auto session = cms::api::create_session(database, "system", system_id, SESSION_LIFETIME_SECONDS);
  REQUIRE(session.has_value());

  // The row must name the kind and exactly the matching id column, which is what sessions_one_account
  // checks; asserting only that a session exists would pass even if the wrong column were set.
  auto found = false;
  for (const auto& row : database(sqlpp::select(cms::db::sessions.accountKind, cms::db::sessions.systemId,
                                                cms::db::sessions.learnerId, cms::db::sessions.adminId)
                                      .from(cms::db::sessions)
                                      .where(cms::db::sessions.id == *session))) {
    found = true;
    CHECK(std::string{row.accountKind} == "system");
    REQUIRE(row.systemId.has_value());
    CHECK(std::string{*row.systemId} == system_id);
    CHECK(!row.learnerId.has_value());
    CHECK(!row.adminId.has_value());
  }
  CHECK(found);
}

TEST_CASE("an account that is not active cannot open a session", "[accounts][live]") {
  if (std::getenv("CMSLEAN_TEST_DSN") == nullptr) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }

  auto database = cms::test::live_database();
  const auto learner_id = std::string{"44444444-5555-4666-8777-888888888888"};
  const auto admin_id = std::string{"55555555-6666-4777-8888-999999999999"};

  database(sqlpp::insert_into(cms::db::learnerAccounts).set(
      cms::db::learnerAccounts.id = learner_id,
      cms::db::learnerAccounts.handle = std::string{"pending_one"},
      cms::db::learnerAccounts.email = std::string{"pending@example.org"},
      cms::db::learnerAccounts.status = std::string{"pending"}));
  database(sqlpp::insert_into(cms::db::adminAccounts).set(
      cms::db::adminAccounts.id = admin_id, cms::db::adminAccounts.handle = std::string{"revoked_one"},
      cms::db::adminAccounts.status = std::string{"revoked"},
      cms::db::adminAccounts.revokedAt = cms::db::now_stamp()));

  // A revoked administrator is the case that protects the panel: the account exists, so a check on
  // presence alone would let it back in.
  const auto learner = cms::api::create_session(database, "learner", learner_id, SESSION_LIFETIME_SECONDS);
  CHECK(!learner.has_value());
  const auto admin = cms::api::create_session(database, "admin", admin_id, SESSION_LIFETIME_SECONDS);
  CHECK(!admin.has_value());
}

TEST_CASE("an unknown account kind is refused by name", "[accounts][live]") {
  if (std::getenv("CMSLEAN_TEST_DSN") == nullptr) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }

  auto database = cms::test::live_database();
  const auto session = cms::api::create_session(database, "operator",
                                                "66666666-7777-4888-8999-aaaaaaaaaaaa",
                                                SESSION_LIFETIME_SECONDS);
  REQUIRE(!session.has_value());
  CHECK(session.error().code == cms::ErrorCode::invalid_argument);
  CHECK(session.error().message.find("operator") != std::string::npos);
}

TEST_CASE("a role grants exactly the capabilities it names", "[authorize][live]") {
  if (std::getenv("CMSLEAN_TEST_DSN") == nullptr) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }

  auto database = cms::test::live_database();
  const auto user_id = std::string{"44444444-5555-4666-8777-888888888888"};
  const auto role_id = std::string{"55555555-6666-4777-8888-999999999999"};

  // Only an administrator holds a role: a learner and a team never reach an admin capability.
  database(sqlpp::insert_into(cms::db::adminAccounts).set(
      cms::db::adminAccounts.id = user_id, cms::db::adminAccounts.handle = std::string{"role_holder"},
      cms::db::adminAccounts.status = std::string{"active"}));
  database(sqlpp::insert_into(cms::db::roles).set(
      cms::db::roles.id = role_id, cms::db::roles.name = std::string{"reader"}));
  database(sqlpp::insert_into(cms::db::adminRoles).set(
      cms::db::adminRoles.adminId = user_id, cms::db::adminRoles.roleId = role_id));
  database(sqlpp::insert_into(cms::db::roleCapabilities).set(
      cms::db::roleCapabilities.roleId = role_id,
      cms::db::roleCapabilities.capability = std::string{"settings:read"}));

  const auto held = cms::api::load_capabilities(database, "admin", user_id);
  REQUIRE(held.has_value());
  CHECK(held->has(cms::Capability::settings_read));
  CHECK(!held->has(cms::Capability::settings_write));

  CHECK(cms::api::authorize(database, "admin", user_id, cms::Capability::settings_read).has_value());
  const auto refused = cms::api::authorize(database, "admin", user_id, cms::Capability::settings_write);
  REQUIRE(!refused.has_value());
  CHECK(refused.error().code == cms::ErrorCode::permission_denied);
  CHECK(refused.error().message.find("settings:write") != std::string::npos);
}

TEST_CASE("a system account holds every capability and an unknown one holds none", "[authorize][live]") {
  if (std::getenv("CMSLEAN_TEST_DSN") == nullptr) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }

  auto database = cms::test::live_database();
  const auto system_id = std::string{"66666666-7777-4888-8999-aaaaaaaaaaaa"};

  database(sqlpp::insert_into(cms::db::systemAccounts).set(
      cms::db::systemAccounts.id = system_id,
      cms::db::systemAccounts.handle = std::string{"system:authority"}));

  const auto all = cms::api::load_capabilities(database, "system", system_id);
  REQUIRE(all.has_value());
  CHECK(all->bits() == cms::all_capabilities().bits());

  const auto missing =
      cms::api::load_capabilities(database, "admin", "99999999-0000-4000-8000-000000000000");
  REQUIRE(!missing.has_value());
  CHECK(missing.error().code == cms::ErrorCode::not_found);
}
