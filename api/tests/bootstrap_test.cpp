// Bootstrap is the one case that needs an empty database, so it has its own binary: every other live
// test seeds principals, and bootstrap refuses a database that already holds one — which is the
// behaviour under test.

#include "live_database.hpp"

#include <cstdlib>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <cms/api/accounts.hpp>

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
}
