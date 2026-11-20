// The generation queue is claimable exactly once: one statement takes a job, a second worker gets nothing while
// the lease lives, an expired lease is taken over, and only the holder can end the attempt. Two claimers race for
// one job in this file, which is the property ADR 0015 claims and that a claim without these guards loses.

#include <cms/generate/claim.hpp>

#include <catch2/catch_test_macros.hpp>

#include <barrier>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <sqlpp23/sqlpp23.h>

#include <cms/db/connection.hpp>
#include <cms/db/tables.hpp>
#include <cms/db/transaction.hpp>
#include <cms/error.hpp>

#include "live_database.hpp"
#include "pipeline_fixture.hpp"

namespace {

constexpr auto WORKER = "worker-one";
constexpr auto OTHER_WORKER = "worker-two";

struct JobRow {
  std::string state;
  std::int64_t attempts{0};
  std::optional<std::string> claimed_by;
  std::optional<cms::db::timestamp> lease_expires_at;
};

auto job_row(cms::db::connection& database, std::int64_t job) -> JobRow {
  auto row = JobRow{};
  for (const auto& found : database(sqlpp::select(cms::db::generationJobs.state,
                                                 cms::db::generationJobs.attempts,
                                                 cms::db::generationJobs.claimedBy,
                                                 cms::db::generationJobs.leaseExpiresAt)
                                       .from(cms::db::generationJobs)
                                       .where(cms::db::generationJobs.id == job))) {
    row.state = std::string{found.state};
    row.attempts = found.attempts;
    row.claimed_by = found.claimedBy.has_value() ? std::optional<std::string>{std::string{*found.claimedBy}}
                                                 : std::nullopt;
    row.lease_expires_at = found.leaseExpiresAt.has_value() ? found.leaseExpiresAt : std::nullopt;
  }
  return row;
}

/// One queued job, and no other: the claim takes the oldest queued row, so a case clears what earlier cases left.
auto fresh_job(cms::db::connection& database, std::string_view task_id) -> std::int64_t {
  database.execute_text("DELETE FROM generation_jobs", {});
  return cms::generate::test::prepare_job(database, task_id);
}

/// @return A job that has reached done the way the record allows: claimed, ended awaiting-evidence, given the verified
/// certificate the job's own guard requires, then finished. A done job cannot exist without one (ADR 0046 §2).
auto finished_job(cms::db::connection& database, std::string_view task_id) -> std::int64_t {
  const auto job = fresh_job(database, task_id);
  REQUIRE(cms::generate::claim_job(database, WORKER).has_value());
  REQUIRE(cms::generate::finish_claim(database, job, WORKER, "awaiting-evidence").has_value());
  database.execute_text("INSERT INTO generation_certificates (job_id, task_id, task_version, outcome, checks, commit_id, "
                        "evidence) VALUES ($1, $2, 1, 'verified', '[\"step 3, 5 and 6 read back\"]'::jsonb, 'deadbeef', "
                        "'{}'::jsonb)",
                        {job, std::string{task_id}});
  // The claim ended at awaiting-evidence, so nothing holds it: the move to done is the direct one the worker makes after
  // the certificate is written, and the guard is what allows it now.
  database.execute_text("UPDATE generation_jobs SET state = 'done' WHERE id = $1", {job});
  return job;
}

/// @return The refusal the database gave for @p statement, empty when it ran.
auto database_refusal(cms::db::connection& database, std::string_view statement,
                      std::vector<cms::db::bound_value> parameters) -> std::string {
  try {
    database.execute_text(std::string{statement}, std::move(parameters));
  } catch (const std::exception& failure) {
    return std::string{failure.what()};
  }
  return {};
}

}  // namespace

TEST_CASE("a claim writes the worker, the moment and the deadline", "[claim][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();
  const auto job = fresh_job(database, "claim-probe-holder");

  const auto claimed = cms::generate::claim_job(database, WORKER);

  REQUIRE(claimed.has_value());
  REQUIRE(claimed->has_value());
  CHECK((*claimed)->id == job);
  CHECK((*claimed)->worker == WORKER);
  CHECK((*claimed)->attempts == 1);
  CHECK((*claimed)->lease_expires_at > cms::db::now_stamp());
  const auto row = job_row(database, job);
  CHECK(row.state == "running");
  CHECK(row.claimed_by.value_or(std::string{}) == WORKER);
  CHECK(row.lease_expires_at.has_value());
}

TEST_CASE("a live generation lease is not taken by another worker", "[claim][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();
  const auto job = fresh_job(database, "claim-probe-live-lease");
  REQUIRE(cms::generate::claim_job(database, WORKER).has_value());

  const auto second = cms::generate::claim_job(database, OTHER_WORKER);

  REQUIRE(second.has_value());
  CHECK_FALSE(second->has_value());
  CHECK(job_row(database, job).claimed_by.value_or(std::string{}) == WORKER);
}

TEST_CASE("an expired lease is taken again, and the attempt count says so", "[claim][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();
  const auto job = fresh_job(database, "claim-probe-expired");
  REQUIRE(cms::generate::claim_job(database, WORKER).has_value());

  database.execute_text("UPDATE generation_jobs SET lease_expires_at = now() - interval '1 minute' "
                        "WHERE id = $1",
                        {job});

  const auto again = cms::generate::claim_job(database, OTHER_WORKER);

  REQUIRE(again.has_value());
  REQUIRE(again->has_value());
  CHECK((*again)->id == job);
  CHECK((*again)->worker == OTHER_WORKER);
  CHECK((*again)->attempts == 2);
}

TEST_CASE("two workers racing for one job: exactly one gets it", "[claim][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto first = cms::generate::test::live_database();
  auto second = cms::generate::test::live_database();
  const auto job = fresh_job(first, "claim-probe-race");

  std::optional<cms::generate::JobClaim> won_by_first;
  std::optional<cms::generate::JobClaim> won_by_second;
  auto start = std::barrier{3};
  auto claim = [&start](cms::db::connection& database, std::string_view worker,
                        std::optional<cms::generate::JobClaim>& held) {
    start.arrive_and_wait();
    const auto claimed = cms::generate::claim_job(database, worker);
    if (claimed.has_value() && claimed->has_value()) {
      held = **claimed;
    }
  };
  auto runner = std::thread{claim, std::ref(first), std::string_view{WORKER}, std::ref(won_by_first)};
  auto other = std::thread{claim, std::ref(second), std::string_view{OTHER_WORKER}, std::ref(won_by_second)};
  start.arrive_and_wait();
  runner.join();
  other.join();

  CHECK((won_by_first.has_value() != won_by_second.has_value()));
  const auto winner = won_by_first.has_value() ? *won_by_first : won_by_second.value_or(cms::generate::JobClaim{});
  CHECK(winner.id == job);
  CHECK(job_row(first, job).attempts == 1);
  CHECK(job_row(first, job).state == "running");
}

TEST_CASE("a row its holder has locked is skipped, not handed out twice", "[claim][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto first = cms::generate::test::live_database();
  auto second = cms::generate::test::live_database();
  const auto job = fresh_job(first, "claim-probe-locked");

  auto transaction = cms::db::Transaction{*cms::generate::test::live_client()};
  transaction.connection().execute_text("UPDATE generation_jobs SET updated_at = now() WHERE id = $1", {job});
  const auto blocked = cms::generate::claim_job(second, OTHER_WORKER);
  REQUIRE(blocked.has_value());
  CHECK_FALSE(blocked->has_value());
  transaction.commit();

  const auto after = cms::generate::claim_job(second, OTHER_WORKER);
  REQUIRE(after.has_value());
  REQUIRE(after->has_value());
  CHECK((*after)->id == job);
}

TEST_CASE("the attempt ends: the claim is cleared and the job moves", "[claim][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();
  const auto job = fresh_job(database, "claim-probe-finish");
  REQUIRE(cms::generate::claim_job(database, WORKER).has_value());

  const auto finished = cms::generate::finish_claim(database, job, WORKER, "awaiting-evidence");

  REQUIRE(finished.has_value());
  const auto row = job_row(database, job);
  CHECK(row.state == "awaiting-evidence");
  CHECK_FALSE(row.claimed_by.has_value());
  CHECK_FALSE(row.lease_expires_at.has_value());
  const auto again = cms::generate::finish_claim(database, job, WORKER, "done");
  REQUIRE(!again.has_value());
  CHECK(again.error().message.find("no claim") != std::string::npos);
}

TEST_CASE("a worker that lost its lease cannot end the attempt", "[claim][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();
  const auto job = fresh_job(database, "claim-probe-lost");
  REQUIRE(cms::generate::claim_job(database, WORKER).has_value());

  const auto stolen = cms::generate::finish_claim(database, job, OTHER_WORKER, "done");

  REQUIRE(!stolen.has_value());
  CHECK(job_row(database, job).claimed_by.value_or(std::string{}) == WORKER);
}

TEST_CASE("a finished or failed job is not claimable", "[claim][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();

  // A done job needs its certificate, so the fixture walks the record's own path for that ending and the plain one for
  // failed; both endings must then refuse a second claim and a stolen one. The failed job is prepared without clearing,
  // because the done one has to survive the fixture that made it.
  const auto job = finished_job(database, "claim-probe-done");
  const auto failed = cms::generate::test::prepare_job(database, "claim-probe-failed");
  REQUIRE(cms::generate::claim_job(database, WORKER).has_value());
  REQUIRE(cms::generate::finish_claim(database, failed, WORKER, "failed").has_value());
  for (const auto finished : {job, failed}) {

    const auto claimed = cms::generate::claim_job(database, OTHER_WORKER);

    REQUIRE(claimed.has_value());
    CHECK_FALSE(claimed->has_value());
    const auto stolen = database_refusal(
        database,
        "UPDATE generation_jobs SET claimed_by = 'thief', claimed_at = now(), "
        "lease_expires_at = now() + interval '1 minute' WHERE id = $1",
        {finished});
    CHECK(stolen.find("nothing reopens it") != std::string::npos);
  }
}

TEST_CASE("a job moves only the way the record allows", "[claim][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();
  const auto job = fresh_job(database, "claim-probe-moves");

  // A legal-but-premature move is what the state machine itself refuses; the certificate guard answers for done and the
  // pipeline gate covers that one.
  const auto skipped_a_step = database_refusal(
      database, "UPDATE generation_jobs SET state = 'awaiting-evidence' WHERE id = $1", {job});
  CHECK(skipped_a_step.find("cannot move from queued to awaiting-evidence") != std::string::npos);

  REQUIRE(cms::generate::claim_job(database, WORKER).has_value());
  REQUIRE(cms::generate::finish_claim(database, job, WORKER, "awaiting-evidence").has_value());
  database.execute_text("INSERT INTO generation_certificates (job_id, task_id, task_version, outcome, checks, commit_id, "
                        "evidence) VALUES ($1, 'claim-probe-moves', 1, 'verified', '[\"step 3, 5 and 6 read back\"]'::jsonb, "
                        "'deadbeef', '{}'::jsonb)",
                        {job});
  const auto finished = database_refusal(database, "UPDATE generation_jobs SET state = 'done' WHERE id = $1", {job});
  CHECK(finished.empty());
  const auto reopened = database_refusal(
      database, "UPDATE generation_jobs SET state = 'running' WHERE id = $1", {job});
  CHECK(reopened.find("nothing reopens it") != std::string::npos);
}

TEST_CASE("a job cannot be created already finished", "[claim][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();
  (void)fresh_job(database, "claim-probe-start-state");

  // The state is the job's history, so a row cannot be born with one: the insert path carries the same rule the update path
  // does, the shape the task side already has.
  for (const auto state : {"done", "failed", "awaiting-evidence"}) {
    const auto refused = database_refusal(
        database, "INSERT INTO generation_jobs (task_id, task_version, state) VALUES ($1, 1, $2)",
        {std::string{"claim-probe-start-state"}, std::string{state}});
    CHECK(refused.find("is created queued, not " + std::string{state}) != std::string::npos);
  }

  // A queued row is the one the record allows, so the rule does not block the normal path.
  const auto job = cms::generate::test::prepare_job(database, "claim-probe-start-queued");
  CHECK(job_row(database, job).state == "queued");
}
