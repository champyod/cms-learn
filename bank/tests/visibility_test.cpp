#include <cms/bank/visibility.hpp>
#include <cms/db/connection.hpp>
#include <cms/db/tables.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <drogon/orm/DbClient.h>
#include <sqlpp23/sqlpp23.h>

namespace {

using cms::bank::AudienceKind;

auto has_live_database() -> bool { return std::getenv("CMSLEAN_TEST_DSN") != nullptr; }

auto live_client() -> const std::shared_ptr<::drogon::orm::DbClient>& {
  static const auto client = [] {
    const auto* dsn = std::getenv("CMSLEAN_TEST_DSN");
    REQUIRE(dsn != nullptr);
    return ::drogon::orm::DbClient::newPgClient(std::string{dsn}, 4);
  }();
  return client;
}

auto live_database() -> cms::db::connection {
  return cms::db::connection{std::make_shared<const cms::db::connection_config>(
      cms::db::connection_config{.client = live_client()})};
}

auto encoded(AudienceKind kind, std::int64_t id) -> std::int64_t {
  const auto value = cms::bank::encode(kind, id);
  REQUIRE(value.has_value());
  return *value;
}

}  // namespace

TEST_CASE("everyone is the zero value") {
  CHECK(cms::bank::everyone() == 0);
  CHECK(encoded(AudienceKind::everyone, 0) == cms::bank::everyone());
  CHECK(!cms::bank::encode(AudienceKind::everyone, 3).has_value());
}

TEST_CASE("an encoded value round-trips through decode") {
  const auto limit = cms::bank::identifier_limit();
  for (const auto id : {std::int64_t{0}, std::int64_t{1}, std::int64_t{7}, limit}) {
    for (const auto kind : {AudienceKind::learner, AudienceKind::administrator, AudienceKind::team}) {
      const auto decoded = cms::bank::decode(encoded(kind, id));
      REQUIRE(decoded.has_value());
      CHECK(decoded->kind == kind);
      CHECK(decoded->id == id);
    }
  }

  const auto zero = cms::bank::decode(cms::bank::everyone());
  REQUIRE(zero.has_value());
  CHECK(zero->kind == AudienceKind::everyone);
  CHECK(zero->id == 0);
}

TEST_CASE("a kind refuses an identifier it cannot carry") {
  CHECK(!cms::bank::encode(AudienceKind::learner, -1).has_value());
  CHECK(!cms::bank::encode(AudienceKind::team, cms::bank::identifier_limit() + 1).has_value());
}

TEST_CASE("decode refuses a value outside the four ranges") {
  CHECK(!cms::bank::decode(1).has_value());
  CHECK(!cms::bank::decode(cms::bank::identifier_limit()).has_value());
  CHECK(!cms::bank::decode(encoded(AudienceKind::team, cms::bank::identifier_limit()) + 1).has_value());
}

TEST_CASE("a request audience is everyone, the viewer and their teams") {
  const auto audience = cms::bank::audience_for(7, std::vector<std::int64_t>{9, 11});
  REQUIRE(audience.has_value());
  REQUIRE(audience->size() == 4);
  CHECK((*audience)[0] == cms::bank::everyone());
  CHECK((*audience)[1] == encoded(AudienceKind::learner, 7));
  CHECK((*audience)[2] == encoded(AudienceKind::team, 9));
  CHECK((*audience)[3] == encoded(AudienceKind::team, 11));
  CHECK(!cms::bank::audience_for(-1, {}).has_value());
}

TEST_CASE("a new task is granted by author kind") {
  const auto learner = cms::bank::default_visibility(cms::bank::AuthorKind::learner);
  REQUIRE(learner.size() == 1);
  CHECK(learner.front() == cms::bank::everyone());
  CHECK(cms::bank::default_visibility(cms::bank::AuthorKind::administrator).empty());
}

TEST_CASE("a principal uuid binds to its serial id and composes an audience", "[visibility][live]") {
  if (!has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = live_database();
  const auto account = std::string{"aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeee90"};
  database(sqlpp::insert_into(cms::db::principals).set(
      cms::db::principals.id = account, cms::db::principals.kind = std::string{"user"},
      cms::db::principals.handle = std::string{"visibility_one"},
      cms::db::principals.status = std::string{"active"}));

  const auto serial = cms::bank::serial_id_of(database, account);
  REQUIRE(serial.has_value());
  CHECK(*serial > 0);

  const auto audience = cms::bank::request_audience(database, account);
  REQUIRE(audience.has_value());
  REQUIRE(audience->size() == 2);
  CHECK((*audience)[0] == cms::bank::everyone());
  const auto learner = cms::bank::encode(cms::bank::AudienceKind::learner, *serial);
  REQUIRE(learner.has_value());
  CHECK((*audience)[1] == *learner);
}

TEST_CASE("a uuid with no principal row cannot compose an audience", "[visibility][live]") {
  if (!has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = live_database();
  const auto unknown = std::string{"aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeee91"};
  CHECK(!cms::bank::serial_id_of(database, unknown).has_value());
  CHECK(!cms::bank::request_audience(database, unknown).has_value());
}
