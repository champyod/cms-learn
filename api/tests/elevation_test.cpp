#include "live_database.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>
#include <sqlpp23/sqlpp23.h>

#include <cms/api/admin_protocol.hpp>
#include <cms/api/elevation.hpp>
#include <cms/db/tables.hpp>
#include <cms/settings_snapshot.hpp>

namespace {

constexpr std::string_view GRANTOR = "88888888-9999-4aaa-8bbb-cccccccccccc";
constexpr std::string_view WITNESS = "99999999-aaaa-4bbb-8ccc-dddddddddddd";

/// Each case gets its own beneficiary so no case depends on the order Catch2 runs them in.
constexpr std::string_view BENEFICIARY_A = "77777777-8888-4999-8aaa-bbbbbbbbbbb1";
constexpr std::string_view BENEFICIARY_B = "77777777-8888-4999-8aaa-bbbbbbbbbbb2";
constexpr std::string_view BENEFICIARY_C = "77777777-8888-4999-8aaa-bbbbbbbbbbb3";
constexpr std::string_view BENEFICIARY_D = "77777777-8888-4999-8aaa-bbbbbbbbbbb4";
constexpr std::string_view BENEFICIARY_E = "77777777-8888-4999-8aaa-bbbbbbbbbbb5";

auto now_stamp() -> cms::db::timestamp {
  return std::chrono::time_point_cast<cms::db::timestamp::duration>(
      std::chrono::system_clock::now());
}

auto stamp_after(std::int64_t seconds) -> cms::db::timestamp {
  return now_stamp() + std::chrono::seconds{seconds};
}

auto ensure_principal(cms::db::connection& database, std::string_view id, std::string_view handle) {
  auto found = false;
  for (const auto& row : database(sqlpp::select(cms::db::principals.id)
                                      .from(cms::db::principals)
                                      .where(cms::db::principals.id == std::string{id}))) {
    (void)row;
    found = true;
  }
  if (!found) {
    database(sqlpp::insert_into(cms::db::principals).set(
        cms::db::principals.id = std::string{id}, cms::db::principals.kind = std::string{"user"},
        cms::db::principals.handle = std::string{handle},
        cms::db::principals.status = std::string{"active"}));
  }
}

auto policy() -> cms::SettingsSnapshot { return cms::SettingsSnapshot{1, {}}; }

/// Gives a beneficiary a known starting state: the account exists and holds no elevation, so a case can
/// run more than once against one database and still assert what it says it asserts.
auto prepare(cms::db::connection& database, std::string_view id, std::string_view handle) {
  ensure_principal(database, id, handle);
  database(sqlpp::delete_from(cms::db::elevations)
               .where(cms::db::elevations.principalId == std::string{id}));
}

auto elevation_for(std::string_view principal_id, std::string_view capability, std::int64_t minutes)
    -> cms::api::ElevationRequest {
  return cms::api::ElevationRequest{.principal_id = std::string{principal_id},
                                    .capability = std::string{capability},
                                    .granted_by = std::string{GRANTOR},
                                    .reason = "rotate the mail credential",
                                    .minutes = minutes};
}

}  // namespace

TEST_CASE("a granted elevation changes no authority until a second person approves it",
          "[elevation][live]") {
  if (!cms::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::test::live_database();
  prepare(database, BENEFICIARY_A, "elevation_one");
  ensure_principal(database, GRANTOR, "elevation_grantor");
  ensure_principal(database, WITNESS, "elevation_witness");

  const auto granted =
      cms::api::grant_elevation(database, policy(), elevation_for(BENEFICIARY_A, "settings:write", 5));
  REQUIRE(granted.has_value());
  CHECK(granted->minutes == 5);
  CHECK(!granted->is_live);

  const auto before = cms::api::load_elevated_capabilities(database, BENEFICIARY_A);
  REQUIRE(before.has_value());
  CHECK(!before->has(cms::Capability::settings_write));

  REQUIRE(cms::api::approve_elevation(database, granted->id, WITNESS).has_value());

  const auto after = cms::api::load_elevated_capabilities(database, BENEFICIARY_A);
  REQUIRE(after.has_value());
  CHECK(after->has(cms::Capability::settings_write));
}

TEST_CASE("two people means neither the grantor nor the beneficiary approves",
          "[elevation][live]") {
  if (!cms::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::test::live_database();
  prepare(database, BENEFICIARY_B, "elevation_two");
  ensure_principal(database, GRANTOR, "elevation_grantor");
  ensure_principal(database, WITNESS, "elevation_witness");

  const auto granted =
      cms::api::grant_elevation(database, policy(), elevation_for(BENEFICIARY_B, "user:read", 5));
  REQUIRE(granted.has_value());

  const auto self = cms::api::approve_elevation(database, granted->id, BENEFICIARY_B);
  REQUIRE(!self.has_value());
  CHECK(self.error().code == cms::ErrorCode::permission_denied);

  const auto grantor = cms::api::approve_elevation(database, granted->id, GRANTOR);
  REQUIRE(!grantor.has_value());
  CHECK(grantor.error().code == cms::ErrorCode::permission_denied);

  REQUIRE(cms::api::approve_elevation(database, granted->id, WITNESS).has_value());

  const auto again = cms::api::approve_elevation(database, granted->id, WITNESS);
  REQUIRE(!again.has_value());
  CHECK(again.error().code == cms::ErrorCode::conflict);
}

TEST_CASE("the database refuses a row that names the same person twice", "[elevation][live]") {
  if (!cms::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::test::live_database();
  prepare(database, BENEFICIARY_C, "elevation_three");
  ensure_principal(database, GRANTOR, "elevation_grantor");

  // The constraint, not the function, is what makes two people a fact: a second writer that skipped
  // approve_elevation would have to violate this to be wrong, and it cannot.
  CHECK_THROWS(database(sqlpp::insert_into(cms::db::elevations).set(
      cms::db::elevations.id = std::string{"aaaaaaa1-bbbb-4ccc-8ddd-eeeeeeeeeee1"},
      cms::db::elevations.principalId = std::string{BENEFICIARY_C},
      cms::db::elevations.capability = std::string{"settings:read"},
      cms::db::elevations.grantedBy = std::string{GRANTOR},
      cms::db::elevations.approvedBy = std::string{GRANTOR},
      cms::db::elevations.expiresAt = stamp_after(300),
      cms::db::elevations.reason = std::string{"self-approved"})));
}

TEST_CASE("an elevation is refused when it could not be a grant", "[elevation][live]") {
  if (!cms::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::test::live_database();
  prepare(database, BENEFICIARY_D, "elevation_four");
  ensure_principal(database, GRANTOR, "elevation_grantor");

  const auto refused_capability = cms::api::grant_elevation(
      database, policy(), elevation_for(BENEFICIARY_D, "settings:everything", 5));
  REQUIRE(!refused_capability.has_value());
  CHECK(refused_capability.error().code == cms::ErrorCode::invalid_argument);

  auto silent = elevation_for(BENEFICIARY_D, "settings:write", 5);
  silent.reason = "   ";
  const auto refused_reason = cms::api::grant_elevation(database, policy(), silent);
  REQUIRE(!refused_reason.has_value());
  CHECK(refused_reason.error().code == cms::ErrorCode::invalid_argument);

  const auto refused_length =
      cms::api::grant_elevation(database, policy(), elevation_for(BENEFICIARY_D, "settings:write", 61));
  REQUIRE(!refused_length.has_value());
  CHECK(refused_length.error().message.find("60") != std::string::npos);

  auto yourself = elevation_for(BENEFICIARY_D, "settings:write", 5);
  yourself.granted_by = std::string{BENEFICIARY_D};
  const auto refused_self = cms::api::grant_elevation(database, policy(), yourself);
  REQUIRE(!refused_self.has_value());
  CHECK(refused_self.error().code == cms::ErrorCode::permission_denied);

  auto nobody = elevation_for("00000000-0000-4000-8000-000000000000", "settings:write", 5);
  const auto refused_unknown = cms::api::grant_elevation(database, policy(), nobody);
  REQUIRE(!refused_unknown.has_value());
  CHECK(refused_unknown.error().code == cms::ErrorCode::not_found);
}

TEST_CASE("an elevation that expired before approval cannot go live", "[elevation][live]") {
  if (!cms::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::test::live_database();
  prepare(database, BENEFICIARY_E, "elevation_five");
  ensure_principal(database, GRANTOR, "elevation_grantor");
  ensure_principal(database, WITNESS, "elevation_witness");
  const auto stale_id = std::string{"aaaaaaa2-bbbb-4ccc-8ddd-eeeeeeeeeee2"};

  database(sqlpp::insert_into(cms::db::elevations).set(
      cms::db::elevations.id = stale_id,
      cms::db::elevations.principalId = std::string{BENEFICIARY_E},
      cms::db::elevations.capability = std::string{"settings:write"},
      cms::db::elevations.grantedBy = std::string{GRANTOR},
      cms::db::elevations.grantedAt = stamp_after(-7200),
      cms::db::elevations.expiresAt = stamp_after(-3600),
      cms::db::elevations.reason = std::string{"left open by the previous operator"}));

  const auto approved = cms::api::approve_elevation(database, stale_id, WITNESS);
  REQUIRE(!approved.has_value());
  CHECK(approved.error().code == cms::ErrorCode::invalid_argument);

  const auto live = cms::api::load_elevated_capabilities(database, BENEFICIARY_E);
  REQUIRE(live.has_value());
  CHECK(!live->has(cms::Capability::settings_write));
}

TEST_CASE("the control protocol answers every action a client can send", "[protocol][live]") {
  if (!cms::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::test::live_database();
  const auto settings = policy();
  prepare(database, BENEFICIARY_A, "elevation_one");
  ensure_principal(database, GRANTOR, "elevation_grantor");

  CHECK(cms::api::dispatch_line(R"({"action":"ping"})", settings, database) ==
        "{\"ok\":true,\"settings_version\":1}\n");

  const auto unknown = cms::api::dispatch_line(R"({"action":"invent"})", settings, database);
  CHECK(unknown.find("\"ok\":false") != std::string::npos);
  CHECK(unknown.find("invent") != std::string::npos);

  const auto nested =
      cms::api::dispatch_line(R"({"action":"ping","extra":{"a":1}})", settings, database);
  CHECK(nested.find("\"ok\":false") != std::string::npos);

  CHECK(cms::api::dispatch_line("{", settings, database).find("\"ok\":false") != std::string::npos);

  // A reason containing a quote is the case a hand-rolled field reader truncates; the parser does not.
  const auto granted = cms::api::dispatch_line(
      R"({"action":"elevation_grant","principal_id":"77777777-8888-4999-8aaa-bbbbbbbbbbb1",)"
      R"("capability":"content:read","granted_by":"88888888-9999-4aaa-8bbb-cccccccccccc",)"
      R"("reason":"the \"audit\" export","minutes":3})",
      settings, database);
  CHECK(granted.find("\"ok\":true") != std::string::npos);
  CHECK(granted.find("\"live\":false") != std::string::npos);

  const auto missing = cms::api::dispatch_line(R"({"action":"elevation_grant"})", settings, database);
  CHECK(missing.find("\"ok\":false") != std::string::npos);
  CHECK(missing.find("principal_id") != std::string::npos);
}
