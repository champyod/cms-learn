#include <cms/bank/bank.hpp>
#include <cms/bank/seed.hpp>
#include <cms/db/connection.hpp>
#include <cms/db/transaction.hpp>
#include <cms/db/tables.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include <drogon/orm/DbClient.h>
#include <sqlpp23/sqlpp23.h>

namespace {

/// The bank that ships with the image, found through the path the build recorded.
auto shipped_bank() -> std::filesystem::path {
  return std::filesystem::path{CMS_CONTENT_DIR};
}

auto has_live_database() -> bool { return std::getenv("CMSLEAN_TEST_DSN") != nullptr; }

/// One client for the whole binary: a second Drogon client in one process deadlocks its pool.
auto live_client() -> const std::shared_ptr<::drogon::orm::DbClient>& {
  const auto* dsn = std::getenv("CMSLEAN_TEST_DSN");
  static const auto client = ::drogon::orm::DbClient::newPgClient(dsn, 4);
  return client;
}

auto live_database() -> cms::db::connection {
  return cms::db::connection{std::make_shared<const cms::db::connection_config>(
      cms::db::connection_config{.client = live_client()})};
}

auto count_tasks(cms::db::connection& database) -> std::size_t {
  auto total = std::size_t{0};
  for (const auto& row : database(sqlpp::select(cms::db::tasks.id).from(cms::db::tasks))) {
    (void)row;
    ++total;
  }
  return total;
}

auto count_examples(cms::db::connection& database) -> std::size_t {
  auto total = std::size_t{0};
  for (const auto& row : database(sqlpp::select(cms::db::taskExamples.taskId)
                                      .from(cms::db::taskExamples))) {
    (void)row;
    ++total;
  }
  return total;
}

auto seed_once(const cms::bank::Bank& bank) -> cms::Result<cms::bank::SeedReport> {
  auto transaction = cms::db::Transaction{*live_client()};
  auto report = cms::bank::seed(transaction.connection(), bank);
  if (!report) {
    return report;
  }
  transaction.commit();
  return report;
}

}  // namespace

TEST_CASE("the shipped bank loads and hashes every file it was built from", "[bank][unit]") {
  const auto bank = cms::bank::load(shipped_bank());
  REQUIRE(bank.has_value());
  CHECK(bank->topics.size() >= 30);
  CHECK(bank->tasks.size() >= 2);
  CHECK(bank->content_hash.size() == 64);

  for (const auto& task : bank->tasks) {
    CHECK(task.content_hash.size() == 64);
    CHECK(!task.statements.empty());
    CHECK(!task.examples.empty());
    CHECK(!task.solution.empty());
    CHECK(task.version >= 1);
  }
}

TEST_CASE("a bank is refused when a file it names is not there", "[bank][unit]") {
  const auto missing = cms::bank::load(std::filesystem::path{"/nonexistent/content"});
  REQUIRE(!missing.has_value());
  CHECK(missing.error().code == cms::ErrorCode::not_found);
}

TEST_CASE("editing one statement changes the digest of the task and of the bank", "[bank][unit]") {
  const auto original = cms::bank::load(shipped_bank());
  REQUIRE(original.has_value());

  const auto scratch = std::filesystem::temp_directory_path() / "cms-bank-digest";
  std::filesystem::remove_all(scratch);
  std::filesystem::copy(shipped_bank(), scratch, std::filesystem::copy_options::recursive);

  const auto first = original->tasks.front();
  {
    auto appended = std::ofstream{scratch / "tasks" / first.id / "statement.en.md", std::ios::app};
    appended << "\nedited\n";
  }

  const auto edited = cms::bank::load(scratch);
  std::filesystem::remove_all(scratch);
  REQUIRE(edited.has_value());
  CHECK(edited->content_hash != original->content_hash);
}

TEST_CASE("seeding twice writes once and reports the same version the second time",
          "[bank][live]") {
  if (!has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  const auto bank = cms::bank::load(shipped_bank());
  REQUIRE(bank.has_value());

  // The first import may be this database's first or its second: the assertion that matters is that a
  // second import of the same bank changes nothing, so the case does not depend on being run first.
  const auto first = seed_once(*bank);
  REQUIRE(first.has_value());

  auto database = live_database();
  auto expected_examples = std::size_t{0};
  for (const auto& task : bank->tasks) {
    expected_examples += task.examples.size();
  }
  CHECK(count_tasks(database) == bank->tasks.size());
  CHECK(count_examples(database) == expected_examples);

  // The plan's falsification: the second import of the same bank must write nothing and must not move
  // the version, or "seeded from the image on every start" would be a rewrite on every start.
  const auto second = seed_once(*bank);
  REQUIRE(second.has_value());
  CHECK(!second->is_changed);
  CHECK(second->version == first->version);
  CHECK(count_tasks(database) == bank->tasks.size());
  CHECK(count_examples(database) == expected_examples);
}
