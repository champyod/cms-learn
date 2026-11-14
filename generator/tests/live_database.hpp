#pragma once

#include <cstdlib>
#include <memory>

#include <catch2/catch_test_macros.hpp>
#include <drogon/orm/DbClient.h>

#include <cms/db/connection.hpp>

namespace cms::generate::test {

/// @brief The pool the live cases run on, named by CMSLEAN_TEST_DSN.
///
/// One client for the whole binary: a second Drogon client deadlocks the pool the first one owns, and the cases
/// need a connection, not a pool each.
[[nodiscard]] inline auto live_client() -> const std::shared_ptr<::drogon::orm::DbClient>& {
  const auto* dsn = std::getenv("CMSLEAN_TEST_DSN");
  REQUIRE(dsn != nullptr);
  // Leaked on purpose: Drogon's pool teardown deadlocks when no case ever issued a query, and one pool left to
  // process exit is not a defect. Keeping it reachable means the destructor never runs.
  static const auto* const owner =
      new std::shared_ptr<::drogon::orm::DbClient>(::drogon::orm::DbClient::newPgClient(dsn, 4));
  static const auto client = *owner;
  return client;
}

/// @brief The database a live case runs against, named by CMSLEAN_TEST_DSN.
[[nodiscard]] inline auto live_database() -> cms::db::connection {
  return cms::db::connection{std::make_shared<const cms::db::connection_config>(
      cms::db::connection_config{.client = live_client()})};
}

/// @return Whether this binary was given a database to run against.
[[nodiscard]] inline auto has_live_database() -> bool {
  return std::getenv("CMSLEAN_TEST_DSN") != nullptr;
}

}  // namespace cms::generate::test
