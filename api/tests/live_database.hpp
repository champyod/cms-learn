#pragma once

#include <cstdlib>
#include <memory>

#include <catch2/catch_test_macros.hpp>
#include <drogon/orm/DbClient.h>

#include <cms/db/connection.hpp>

namespace cms::test {

/// @brief The database a live test runs against, named by CMSLEAN_TEST_DSN.
///
/// One client for the whole binary: creating a second Drogon client in a process that already holds one
/// deadlocks its pool, and the tests only need a connection, not a pool each.
///
/// Each test binary wants its own database, used once. The cases seed fixed principals, bootstrap
/// refuses a database that already holds one — which is the behaviour under test — and the audit log is
/// append-only, so a fixed fixture cannot be cleared away for a second run.
inline auto live_database() -> cms::db::connection {
  const auto* dsn = std::getenv("CMSLEAN_TEST_DSN");
  REQUIRE(dsn != nullptr);
  static const auto client = ::drogon::orm::DbClient::newPgClient(dsn, 4);
  return cms::db::connection{std::make_shared<const cms::db::connection_config>(
      cms::db::connection_config{.client = client})};
}

/// @brief Whether this binary was given a database to run against.
inline auto has_live_database() -> bool { return std::getenv("CMSLEAN_TEST_DSN") != nullptr; }

}  // namespace cms::test
