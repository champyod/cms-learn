#include <cms/judge/worker.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <drogon/orm/DbClient.h>
#include <sqlpp23/sqlpp23.h>

#include <cms/db/connection.hpp>
#include <cms/db/tables.hpp>

namespace {

constexpr std::string_view ACCOUNT = "11111111-1111-4111-8111-111111111100";
constexpr std::string_view TASK_ID = "claim-worker-test";
constexpr std::int64_t TASK_VERSION = 1;
constexpr std::string_view WORKER = "worker-a";
constexpr std::string_view OTHER_WORKER = "worker-b";

auto has_live_database() -> bool { return std::getenv("CMSLEAN_TEST_DSN") != nullptr; }

/// One client for the whole binary: a second Drogon client in one process deadlocks its pool.
auto live_client() -> const std::shared_ptr<::drogon::orm::DbClient>& {
  const auto* dsn = std::getenv("CMSLEAN_TEST_DSN");
  REQUIRE(dsn != nullptr);
  static const auto client = ::drogon::orm::DbClient::newPgClient(dsn, 4);
  return client;
}

auto live_database() -> cms::db::connection {
  return cms::db::connection{std::make_shared<const cms::db::connection_config>(
      cms::db::connection_config{.client = live_client()})};
}

auto given_account(cms::db::connection& database) -> void {
  for (const auto& row : database(sqlpp::select(cms::db::learnerAccounts.id)
                                      .from(cms::db::learnerAccounts)
                                      .where(cms::db::learnerAccounts.id == std::string{ACCOUNT}))) {
    (void)row;
    return;
  }
  // A submission belongs to a learner account, so the fixture that submits is a learner.
  database(sqlpp::insert_into(cms::db::learnerAccounts)
               .set(cms::db::learnerAccounts.id = std::string{ACCOUNT},
                    cms::db::learnerAccounts.handle = std::string{"claim_worker"},
                    cms::db::learnerAccounts.email = std::string{"claim_worker@example.org"},
                    cms::db::learnerAccounts.status = std::string{"active"}));
}

auto given_task(cms::db::connection& database) -> void {
  for (const auto& row :
       database(sqlpp::select(cms::db::tasks.id)
                    .from(cms::db::tasks)
                    .where(cms::db::tasks.id == std::string{TASK_ID} &&
                           cms::db::tasks.version == TASK_VERSION))) {
    (void)row;
    return;
  }
  database(sqlpp::insert_into(cms::db::tasks)
               .set(cms::db::tasks.id = std::string{TASK_ID},
                    cms::db::tasks.version = TASK_VERSION,
                    cms::db::tasks.status = std::string{"draft"},
                    cms::db::tasks.titleEn = std::string{"Claim probe"},
                    cms::db::tasks.titleTh = std::string{"ทดสอบการจองงาน"},
                    cms::db::tasks.difficulty = std::int64_t{800},
                    cms::db::tasks.timeLimitMs = std::int64_t{1000},
                    cms::db::tasks.memoryUsedLimitMb = std::int64_t{256},
                    cms::db::tasks.memoryReservedLimitMb = std::int64_t{512},
                    cms::db::tasks.checker = std::string{"standard"},
                    cms::db::tasks.origin = std::string{"original"},
                    cms::db::tasks.verification = std::string{"pending"},
                    cms::db::tasks.solution = std::string{"int main() { return 0; }"},
                    cms::db::tasks.contentHash = std::string(64, '0')));
}

/// A row inserted under a fixed id, deleted first so a second run against the same database starts from the
/// same state rather than failing on the primary key.
auto insert_submission(cms::db::connection& database, std::string_view id, std::string_view source)
    -> void {
  database(sqlpp::delete_from(cms::db::submissions).where(cms::db::submissions.id == std::string{id}));
  database(sqlpp::insert_into(cms::db::submissions)
               .set(cms::db::submissions.id = std::string{id},
                    cms::db::submissions.accountId = std::string{ACCOUNT},
                    cms::db::submissions.taskId = std::string{TASK_ID},
                    cms::db::submissions.taskVersion = TASK_VERSION,
                    cms::db::submissions.source = std::string{source}));
}

/// Takes every claimable row, so a case starts from a known-empty queue instead of the leftovers of the case
/// before it. Claimed rows stay claimed for the lease, so this terminates.
auto drain_queue(cms::db::connection& database) -> void {
  while (true) {
    const auto claim = cms::judge::claim_submission(database, WORKER);
    REQUIRE(claim.has_value());
    if (!claim->has_value()) {
      return;
    }
  }
}

auto backdate_claim(cms::db::connection& database, std::string_view id, std::chrono::minutes age)
    -> void {
  const auto moment = std::chrono::time_point_cast<cms::db::timestamp::duration>(
      std::chrono::system_clock::now() - age);
  database.execute_text("UPDATE submissions SET claimed_at = $1 WHERE id = $2",
                        {moment, std::string{id}});
}

auto is_claimed(cms::db::connection& database, std::string_view id) -> bool {
  for (const auto& row : database(sqlpp::select(cms::db::submissions.claimedBy)
                                      .from(cms::db::submissions)
                                      .where(cms::db::submissions.id == std::string{id}))) {
    return row.claimedBy.has_value();
  }
  return false;
}

/// Everything a claim case needs, from one call, so no case can forget a fixture and fail for that reason.
auto prepare(cms::db::connection& database) -> void {
  given_account(database);
  given_task(database);
  drain_queue(database);
}

}  // namespace

TEST_CASE("two claims take two different submissions", "[claim][live]") {
  if (!has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = live_database();
  prepare(database);

  insert_submission(database, "11111111-1111-4111-8111-111111111101", "int main() { return 1; }");
  insert_submission(database, "11111111-1111-4111-8111-111111111102", "int main() { return 2; }");

  const auto first = cms::judge::claim_submission(database, WORKER);
  REQUIRE(first.has_value());
  REQUIRE(first->has_value());
  const auto second = cms::judge::claim_submission(database, OTHER_WORKER);
  REQUIRE(second.has_value());
  REQUIRE(second->has_value());

  REQUIRE((*first)->id != (*second)->id);
  REQUIRE((*first)->account_id == ACCOUNT);
  REQUIRE((*first)->task_id == TASK_ID);
  REQUIRE((*first)->task_version == TASK_VERSION);
  REQUIRE(((*first)->source == "int main() { return 1; }" ||
           (*first)->source == "int main() { return 2; }"));
}

TEST_CASE("a claim on an empty queue returns nothing", "[claim][live]") {
  if (!has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = live_database();
  prepare(database);

  const auto claim = cms::judge::claim_submission(database, WORKER);
  REQUIRE(claim.has_value());
  REQUIRE_FALSE(claim->has_value());
}

TEST_CASE("an expired lease lets the row be claimed again", "[claim][live]") {
  if (!has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = live_database();
  prepare(database);

  constexpr std::string_view ROW = "11111111-1111-4111-8111-111111111103";
  insert_submission(database, ROW, "int main() { return 3; }");

  const auto first = cms::judge::claim_submission(database, WORKER);
  REQUIRE(first.has_value());
  REQUIRE(first->has_value());
  REQUIRE((*first)->id == ROW);

  backdate_claim(database, ROW, std::chrono::minutes{10});

  const auto again = cms::judge::claim_submission(database, OTHER_WORKER);
  REQUIRE(again.has_value());
  REQUIRE(again->has_value());
  REQUIRE((*again)->id == ROW);
}

TEST_CASE("a live lease is not taken by another worker", "[claim][live]") {
  if (!has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = live_database();
  prepare(database);

  constexpr std::string_view ROW = "11111111-1111-4111-8111-111111111104";
  insert_submission(database, ROW, "int main() { return 4; }");

  const auto first = cms::judge::claim_submission(database, WORKER);
  REQUIRE(first.has_value());
  REQUIRE(first->has_value());
  REQUIRE((*first)->id == ROW);

  const auto second = cms::judge::claim_submission(database, OTHER_WORKER);
  REQUIRE(second.has_value());
  REQUIRE_FALSE(second->has_value());
}

TEST_CASE("releasing a stale claim clears both columns and requeues the row", "[claim][live]") {
  if (!has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = live_database();
  prepare(database);

  constexpr std::string_view ROW = "11111111-1111-4111-8111-111111111105";
  insert_submission(database, ROW, "int main() { return 5; }");

  const auto first = cms::judge::claim_submission(database, WORKER);
  REQUIRE(first.has_value());
  REQUIRE(first->has_value());

  const auto fresh = cms::judge::release_stale_claims(database, cms::judge::CLAIM_LEASE);
  REQUIRE(fresh.has_value());
  REQUIRE(*fresh == 0);
  REQUIRE(is_claimed(database, ROW));

  backdate_claim(database, ROW, std::chrono::minutes{10});
  const auto released = cms::judge::release_stale_claims(database, cms::judge::CLAIM_LEASE);
  REQUIRE(released.has_value());
  REQUIRE(*released == 1);
  REQUIRE_FALSE(is_claimed(database, ROW));

  const auto again = cms::judge::claim_submission(database, OTHER_WORKER);
  REQUIRE(again.has_value());
  REQUIRE(again->has_value());
  REQUIRE((*again)->id == ROW);
}

TEST_CASE("an acceptance records the solve, and a later rejection keeps it",
          "[claim][live]") {
  if (!has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = live_database();
  prepare(database);
  database.execute_text("DELETE FROM progress WHERE account_id = $1::uuid", {std::string{ACCOUNT}});

  const auto accepted_id = std::string{"33333333-3333-4333-8333-333333333300"};
  insert_submission(database, accepted_id, "int main() { return 0; }");
  const auto claimed = cms::judge::claim_submission(database, WORKER);
  REQUIRE(claimed.has_value());
  REQUIRE(claimed->has_value());
  const auto accepted = cms::judge::finish_submission(
      database, claimed->value().id,
      cms::judge::Judgement{.verdict = cms::judge::SubmissionVerdict::accepted, .detail = "ok"});
  REQUIRE(accepted.has_value());

  auto status = std::string{};
  auto solved_at = std::optional<cms::db::timestamp>{};
  auto attempted_at = std::optional<cms::db::timestamp>{};
  const auto read_progress = [&]() {
    status.clear();
    solved_at.reset();
    attempted_at.reset();
    for (const auto& row : database(sqlpp::select(cms::db::progress.status, cms::db::progress.solvedAt,
                                                  cms::db::progress.attemptedAt)
                                        .from(cms::db::progress)
                                        .where(cms::db::progress.accountId == std::string{ACCOUNT} &&
                                               cms::db::progress.taskId == std::string{TASK_ID} &&
                                               cms::db::progress.taskVersion == TASK_VERSION))) {
      status = std::string{row.status};
      solved_at = row.solvedAt;
      attempted_at = row.attemptedAt;
    }
  };

  read_progress();
  CHECK(status == "solved");
  CHECK(solved_at.has_value());
  const auto first_solve = solved_at;

  // A rejected submission on the same task version must leave the row solved, with the same solve time.
  const auto rejected_id = std::string{"33333333-3333-4333-8333-333333333301"};
  insert_submission(database, rejected_id, "int main() { return 1; }");
  const auto re_claimed = cms::judge::claim_submission(database, WORKER);
  REQUIRE(re_claimed.has_value());
  REQUIRE(re_claimed->has_value());
  const auto rejected = cms::judge::finish_submission(
      database, re_claimed->value().id,
      cms::judge::Judgement{.verdict = cms::judge::SubmissionVerdict::wrong_answer, .detail = "wa"});
  REQUIRE(rejected.has_value());

  read_progress();
  CHECK(status == "solved");
  CHECK(solved_at == first_solve);
}
