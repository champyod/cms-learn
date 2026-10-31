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

  const auto premature = cms::api::create_session(database, signup->principal_id,
                                                  SESSION_LIFETIME_SECONDS);
  CHECK(!premature.has_value());

  const auto approved =
      cms::api::approve_account(database, signup->principal_id, signup->principal_id);
  REQUIRE(approved.has_value());

  const auto session = cms::api::create_session(database, signup->principal_id,
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

TEST_CASE("a system principal cannot hold a session", "[accounts][live]") {
  if (std::getenv("CMSLEAN_TEST_DSN") == nullptr) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }

  auto database = cms::test::live_database();
  const auto system_id = std::string{"33333333-4444-4555-8666-777777777777"};

  database(sqlpp::insert_into(cms::db::principals).set(
      cms::db::principals.id = system_id, cms::db::principals.kind = std::string{"system"},
      cms::db::principals.handle = std::string{"system:probe"},
      cms::db::principals.status = std::string{"active"}));

  const auto session = cms::api::create_session(database, system_id, SESSION_LIFETIME_SECONDS);
  CHECK(!session.has_value());
}

TEST_CASE("a role grants exactly the capabilities it names", "[authorize][live]") {
  if (std::getenv("CMSLEAN_TEST_DSN") == nullptr) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }

  auto database = cms::test::live_database();
  const auto user_id = std::string{"44444444-5555-4666-8777-888888888888"};
  const auto role_id = std::string{"55555555-6666-4777-8888-999999999999"};

  database(sqlpp::insert_into(cms::db::principals).set(
      cms::db::principals.id = user_id, cms::db::principals.kind = std::string{"user"},
      cms::db::principals.handle = std::string{"role_holder"},
      cms::db::principals.status = std::string{"active"}));
  database(sqlpp::insert_into(cms::db::roles).set(
      cms::db::roles.id = role_id, cms::db::roles.name = std::string{"reader"}));
  database(sqlpp::insert_into(cms::db::principalRoles).set(
      cms::db::principalRoles.principalId = user_id,
      cms::db::principalRoles.roleId = role_id));
  database(sqlpp::insert_into(cms::db::roleCapabilities).set(
      cms::db::roleCapabilities.roleId = role_id,
      cms::db::roleCapabilities.capability = std::string{"settings:read"}));

  const auto held = cms::api::load_capabilities(database, user_id);
  REQUIRE(held.has_value());
  CHECK(held->has(cms::Capability::settings_read));
  CHECK(!held->has(cms::Capability::settings_write));

  CHECK(cms::api::authorize(database, user_id, cms::Capability::settings_read).has_value());
  const auto refused = cms::api::authorize(database, user_id, cms::Capability::settings_write);
  REQUIRE(!refused.has_value());
  CHECK(refused.error().code == cms::ErrorCode::permission_denied);
  CHECK(refused.error().message.find("settings:write") != std::string::npos);
}

TEST_CASE("a system principal holds every capability and an unknown one holds none", "[authorize][live]") {
  if (std::getenv("CMSLEAN_TEST_DSN") == nullptr) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }

  auto database = cms::test::live_database();
  const auto system_id = std::string{"66666666-7777-4888-8999-aaaaaaaaaaaa"};

  database(sqlpp::insert_into(cms::db::principals).set(
      cms::db::principals.id = system_id, cms::db::principals.kind = std::string{"system"},
      cms::db::principals.handle = std::string{"system:authority"},
      cms::db::principals.status = std::string{"active"}));

  const auto all = cms::api::load_capabilities(database, system_id);
  REQUIRE(all.has_value());
  CHECK(all->bits() == cms::all_capabilities().bits());

  const auto missing = cms::api::load_capabilities(database, "99999999-0000-4000-8000-000000000000");
  REQUIRE(!missing.has_value());
  CHECK(missing.error().code == cms::ErrorCode::not_found);
}
