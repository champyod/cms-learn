#pragma once

#include <cstdlib>
#include <memory>

#include <catch2/catch_test_macros.hpp>
#include <drogon/orm/DbClient.h>

#include <cms/db/connection.hpp>

namespace cms::test {

/// The one variable a live case reads to find its database. The harness sets it per binary, so no case has an
/// address of its own and a run without it is a skip rather than a connection to whatever is listening.
constexpr char LIVE_DATABASE_VARIABLE[] = "CMSLEAN_TEST_DSN";

/// One pool per test binary: Drogon deadlocks a process that creates a second client, and a case needs a
/// connection, not a pool each.
constexpr int LIVE_DATABASE_CONNECTIONS = 4;

/// @brief Whether this binary was given a database to run against.
[[nodiscard]] inline auto has_live_database() -> bool {
  return std::getenv(LIVE_DATABASE_VARIABLE) != nullptr;
}

/// @brief The pool the live cases run on, named by CMSLEAN_TEST_DSN.
///
/// One client for the whole binary: creating a second Drogon client in a process that already holds one
/// deadlocks its pool, and the tests only need a connection, not a pool each.
[[nodiscard]] inline auto live_client() -> const std::shared_ptr<::drogon::orm::DbClient>& {
  const auto* dsn = std::getenv(LIVE_DATABASE_VARIABLE);
  REQUIRE(dsn != nullptr);
  // Leaked on purpose: Drogon's pool teardown deadlocks ("Resource deadlock avoided") when no case ever
  // issued a query, and one pool left to process exit is not a defect. Keeping it reachable means the
  // destructor never runs, so a query-free case exits like every other one instead of racing teardown.
  static const auto* const owner = new std::shared_ptr<::drogon::orm::DbClient>(
      ::drogon::orm::DbClient::newPgClient(dsn, LIVE_DATABASE_CONNECTIONS));
  static const auto client = *owner;
  return client;
}

/// @brief The database a live case runs against, named by CMSLEAN_TEST_DSN.
///
/// Each test binary wants its own database, used once. The cases seed fixed principals, bootstrap refuses a
/// database that already holds one — which is the behaviour under test — and the audit log is append-only, so a
/// fixed fixture cannot be cleared away for a second run.
[[nodiscard]] inline auto live_database() -> cms::db::connection {
  return cms::db::connection{std::make_shared<const cms::db::connection_config>(
      cms::db::connection_config{.client = live_client()})};
}

}  // namespace cms::test
