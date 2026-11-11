// Bootstrap is the one case that needs an empty database, so it has its own binary: every other live
// test seeds principals, and bootstrap refuses a database that already holds one — which is the
// behaviour under test.

#include "live_database.hpp"

#include <cstdlib>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <sqlpp23/sqlpp23.h>

#include <cms/api/accounts.hpp>
#include <cms/capability.hpp>
#include <cms/db/tables.hpp>

TEST_CASE("bootstrap runs once and then refuses", "[bootstrap][live]") {
  if (!cms::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }

  auto database = cms::test::live_database();
  const auto first = cms::api::bootstrap(database, "operator@example.org", "first_admin");
  REQUIRE(first.has_value());
  CHECK(first->size() == 36);

  // The falsification the plan names: the seed runs again on a populated database and must refuse
  // rather than quietly adding a second administrator.
  const auto second = cms::api::bootstrap(database, "second@example.org", "second_admin");
  REQUIRE(!second.has_value());
  CHECK(second.error().code == cms::ErrorCode::conflict);

  // The first administrator is created holding nothing, so the first run seeds the two roles that make any
  // capability reachable: operator holds every capability and is granted to it, author holds reading and
  // writing and is granted to nobody until an administrator grants it from the panel.
  auto role_id = [&database](std::string_view name) {
    auto id = std::string{};
    for (const auto& row : database(sqlpp::select(cms::db::roles.id)
                                        .from(cms::db::roles)
                                        .where(cms::db::roles.name == std::string{name}))) {
      id = std::string{row.id};
    }
    return id;
  };
  const auto operator_role = role_id("operator");
  const auto author_role = role_id("author");
  REQUIRE(!operator_role.empty());
  REQUIRE(!author_role.empty());

  auto capabilities_of = [&database](const std::string& role) {
    auto count = std::int64_t{0};
    for (const auto& row : database(sqlpp::select(sqlpp::count(sqlpp::value(1)).as(sqlpp::alias::a))
                                        .from(cms::db::roleCapabilities)
                                        .where(cms::db::roleCapabilities.roleId == role))) {
      count = row.a;
    }
    return count;
  };
  CHECK(capabilities_of(operator_role) == static_cast<std::int64_t>(cms::k_capability_count));
  CHECK(capabilities_of(author_role) == 2);

  auto holders_of = [&database](const std::string& role) {
    auto count = std::int64_t{0};
    for (const auto& row : database(sqlpp::select(sqlpp::count(sqlpp::value(1)).as(sqlpp::alias::a))
                                        .from(cms::db::adminRoles)
                                        .where(cms::db::adminRoles.roleId == role))) {
      count = row.a;
    }
    return count;
  };
  CHECK(holders_of(operator_role) == 1);
  CHECK(holders_of(author_role) == 0);
}
