#include <cms/api/accounts.hpp>
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

auto live_database() -> cms::db::connection {
  const auto* dsn = std::getenv("CMSLEAN_TEST_DSN");
  REQUIRE(dsn != nullptr);
  // One client for the whole binary: creating a second Drogon client in a process that already holds
  // one deadlocks its pool, and the tests only need a connection, not a pool each.
  static const auto client = ::drogon::orm::DbClient::newPgClient(dsn, 4);
  return cms::db::connection{
      std::make_shared<const cms::db::connection_config>(cms::db::connection_config{client})};
}

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

  auto database = live_database();
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

  auto database = live_database();
  const auto settings = registration_settings(false);

  const auto refused = cms::api::sign_up(database, settings, "student@elsewhere.test", "outsider");
  REQUIRE(!refused.has_value());
  CHECK(refused.error().code == cms::ErrorCode::permission_denied);
}

TEST_CASE("a closed registration refuses everyone", "[accounts][live]") {
  if (std::getenv("CMSLEAN_TEST_DSN") == nullptr) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }

  auto database = live_database();
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

  auto database = live_database();
  const auto system_id = std::string{"33333333-4444-4555-8666-777777777777"};

  database(sqlpp::insert_into(cms::db::principals).set(
      cms::db::principals.id = system_id, cms::db::principals.kind = std::string{"system"},
      cms::db::principals.handle = std::string{"system:probe"},
      cms::db::principals.status = std::string{"active"}));

  const auto session = cms::api::create_session(database, system_id, SESSION_LIFETIME_SECONDS);
  CHECK(!session.has_value());
}
