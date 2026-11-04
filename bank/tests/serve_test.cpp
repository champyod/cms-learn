#include <cms/bank/bank.hpp>
#include <cms/bank/seed.hpp>
#include <cms/bank/serve.hpp>
#include <cms/bank/visibility.hpp>
#include <cms/db/connection.hpp>
#include <cms/db/transaction.hpp>
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

auto shipped_bank() -> std::filesystem::path {
  return std::filesystem::path{CMS_CONTENT_DIR};
}

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

/// A task the query can find: published, one topic, one difficulty, and an id that sorts predictably.
/// The row is inserted without a visibility row; a caller that wants it served grants it explicitly.
auto given_hidden_task(cms::db::connection& database, std::string_view id, std::int32_t difficulty,
                       std::string_view topic) {
  const auto identifier = std::string{id};
  database(sqlpp::insert_into(cms::db::tasks).set(
      cms::db::tasks.id = identifier, cms::db::tasks.version = 1,
      cms::db::tasks.status = std::string{"published"},
      cms::db::tasks.titleEn = identifier, cms::db::tasks.titleTh = identifier,
      cms::db::tasks.difficulty = difficulty, cms::db::tasks.timeLimitMs = 1000,
      cms::db::tasks.memoryLimitMb = 256, cms::db::tasks.checker = std::string{"token"},
      cms::db::tasks.origin = std::string{"original"},
      cms::db::tasks.verification = std::string{"verified"},
      cms::db::tasks.solution = std::string{"int main() {}"},
      cms::db::tasks.contentHash = std::string(64, 'a')));
  database(sqlpp::insert_into(cms::db::taskTopics).set(
      cms::db::taskTopics.taskId = identifier, cms::db::taskTopics.taskVersion = 1,
      cms::db::taskTopics.topic = std::string{topic}));
}

/// The audience a viewer runs with: everyone plus themselves.
auto viewer_audience() -> std::vector<std::int64_t> {
  const auto audience = cms::bank::audience_for(7, {});
  REQUIRE(audience.has_value());
  return *audience;
}

auto given_task(cms::db::connection& database, std::string_view id, std::int32_t difficulty,
                std::string_view topic) {
  given_hidden_task(database, id, difficulty, topic);
  database(sqlpp::insert_into(cms::db::taskVisibility).set(
      cms::db::taskVisibility.taskId = std::string{id},
      cms::db::taskVisibility.principalId = cms::bank::everyone()));
}

auto attempted(cms::db::connection& database, std::string_view account, std::string_view id) {
  database(sqlpp::insert_into(cms::db::progress).set(
      cms::db::progress.accountId = std::string{account},
      cms::db::progress.taskId = std::string{id},
      cms::db::progress.taskVersion = 1, cms::db::progress.status = std::string{"attempted"}));
}

/// Imports the shipped bank when it is not already there. Cheap when it is: the seeder compares digests
/// and writes nothing, so a case never depends on another case having run first.
void ensure_seeded() {
  const auto bank = cms::bank::load(shipped_bank());
  REQUIRE(bank.has_value());
  auto transaction = cms::db::Transaction{*live_client()};
  REQUIRE(cms::bank::seed(transaction.connection(), *bank).has_value());
  transaction.commit();
}

auto stage(std::int32_t minimum, std::int32_t maximum,
           const std::vector<std::string>& tags = {"io"}) -> cms::bank::Stage {
  auto stage = cms::bank::Stage{};
  stage.id = "st-probe";
  stage.tags = tags;
  stage.difficulty_min = minimum;
  stage.difficulty_max = maximum;
  return stage;
}

}  // namespace

TEST_CASE("the next task is the one in band the account has not touched", "[serve][live]") {
  if (!has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = live_database();
  ensure_seeded();

  const auto account = std::string{"aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeee1"};
  database(sqlpp::insert_into(cms::db::principals).set(
      cms::db::principals.id = account, cms::db::principals.kind = std::string{"user"},
      cms::db::principals.handle = std::string{"serve_one"},
      cms::db::principals.status = std::string{"active"}));

  given_task(database, "probe-easy", 800, "io");
  given_task(database, "probe-mid", 900, "io");
  given_task(database, "probe-high", 1100, "io");

  const auto band = stage(800, 900);
  const auto audience = viewer_audience();
  const auto first = cms::bank::next_unseen_task(database, account, band, audience);
  REQUIRE(first.has_value());
  REQUIRE(first->has_value());
  CHECK((*first)->id == "probe-easy");
  CHECK(!(*first)->is_widened);

  attempted(database, account, "probe-easy");
  const auto second = cms::bank::next_unseen_task(database, account, band, audience);
  REQUIRE(second.has_value());
  REQUIRE(second->has_value());
  CHECK((*second)->id == "probe-mid");
  CHECK(!(*second)->is_widened);

  // The band is exhausted, so it widens once: 600..1100 reaches the harder task and nothing else.
  attempted(database, account, "probe-mid");
  const auto widened = cms::bank::next_unseen_task(database, account, band, audience);
  REQUIRE(widened.has_value());
  REQUIRE(widened->has_value());
  CHECK((*widened)->id == "probe-high");
  CHECK((*widened)->is_widened);

  attempted(database, account, "probe-high");
  const auto exhausted = cms::bank::next_unseen_task(database, account, band, audience);
  REQUIRE(exhausted.has_value());
  CHECK(!exhausted->has_value());
}

TEST_CASE("a draft task is never served", "[serve][live]") {
  if (!has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = live_database();
  ensure_seeded();
  const auto account = std::string{"aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeee2"};
  database(sqlpp::insert_into(cms::db::principals).set(
      cms::db::principals.id = account, cms::db::principals.kind = std::string{"user"},
      cms::db::principals.handle = std::string{"serve_two"},
      cms::db::principals.status = std::string{"active"}));

  given_task(database, "probe-draft", 800, "sorting");
  database(sqlpp::update(cms::db::tasks)
               .set(cms::db::tasks.status = std::string{"draft"})
               .where(cms::db::tasks.id == std::string{"probe-draft"}));

  const auto audience = viewer_audience();
  const auto result = cms::bank::next_unseen_task(database, account, stage(800, 1200, {"sorting"}),
                                                  audience);
  REQUIRE(result.has_value());
  CHECK(!result->has_value());
}

TEST_CASE("a task with no visibility row is never served", "[serve][live]") {
  if (!has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = live_database();
  ensure_seeded();
  const auto account = std::string{"aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeee3"};
  database(sqlpp::insert_into(cms::db::principals).set(
      cms::db::principals.id = account, cms::db::principals.kind = std::string{"user"},
      cms::db::principals.handle = std::string{"serve_three"},
      cms::db::principals.status = std::string{"active"}));

  given_hidden_task(database, "probe-hidden", 800, "geometry");

  // A tag set of its own, so the tasks the other cases left behind cannot answer.
  const auto result =
      cms::bank::next_unseen_task(database, account, stage(800, 1200, {"geometry"}), viewer_audience());
  REQUIRE(result.has_value());
  CHECK(!result->has_value());
}

TEST_CASE("the serving query follows the audience composed from a principal", "[serve][live]") {
  if (!has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = live_database();
  ensure_seeded();
  const auto account = std::string{"aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeee4"};
  database(sqlpp::insert_into(cms::db::principals).set(
      cms::db::principals.id = account, cms::db::principals.kind = std::string{"user"},
      cms::db::principals.handle = std::string{"serve_four"},
      cms::db::principals.status = std::string{"active"}));

  // The lower task has no visibility row; the higher one is granted to everyone. A query that lost its
  // predicate returns the lower id, so the returned id names the defect.
  given_hidden_task(database, "probe-composed-hidden", 1000, "io");
  given_task(database, "probe-composed-public", 1100, "io");

  const auto audience = cms::bank::request_audience(database, account);
  REQUIRE(audience.has_value());
  const auto result = cms::bank::next_unseen_task(database, account, stage(950, 1200, {"io"}),
                                                  *audience);
  REQUIRE(result.has_value());
  REQUIRE(result->has_value());
  CHECK((*result)->id != "probe-composed-hidden");
  CHECK((*result)->id == "probe-composed-public");
}
