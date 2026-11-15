// What steps 1 and 2 produce is written to generation_candidates, one row per candidate, and the plan column is
// stored as jsonb: a plan that is not JSON, or a decision that is half written, never becomes a row.

#include <cms/generate/brief.hpp>
#include <cms/generate/candidate.hpp>
#include <cms/generate/candidates.hpp>
#include <cms/generate/judge.hpp>
#include <cms/generate/model.hpp>
#include <cms/generate/planner.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <sqlpp23/sqlpp23.h>

#include <cms/db/connection.hpp>
#include <cms/db/tables.hpp>
#include <cms/db/transaction.hpp>

#include "live_database.hpp"
#include "pipeline_fixture.hpp"

namespace {

auto brief() -> cms::generate::Brief {
  auto value = cms::generate::Brief{};
  value.technique = "prefix-sums";
  value.difficulty = "1800";
  value.topics = {"prefix-sums"};
  value.locales = {"en", "th"};
  return value;
}

auto plan_answer(std::string_view approach) -> std::string {
  return "{\"approach\": \"" + std::string{approach} +
         "\", \"intended_complexity\": \"O(n log n)\", \"fits_bounds\": true}";
}

auto decision_answer() -> std::string {
  return "{\"chosen\": 2, \"reason\": \"the cheapest plan that fits\", \"rejected\": "
         "[{\"position\": 1, \"reason\": \"slower than the bound allows\"}, "
         "{\"position\": 3, \"reason\": \"more machinery than the task needs\"}, "
         "{\"position\": 4, \"reason\": \"no simpler proof\"}]}";
}

auto plan_and_decide() -> cms::generate::Judgement {
  auto planner = cms::generate::RecordedClient{
      "planner-model", "planner-v1", 42,
      {plan_answer("prefix sums"), plan_answer("segment tree"), plan_answer("sqrt decomposition"),
       plan_answer("mo's algorithm")}};
  const auto planned = cms::generate::plan_candidates(planner, brief());
  REQUIRE(planned.has_value());

  auto judge = cms::generate::RecordedClient{"judge-model", "judge-v1", 42, {decision_answer()}};
  auto judged = cms::generate::judge_candidates(judge, *planned);
  REQUIRE(judged.has_value());
  return std::move(*judged);
}

struct StoredRow {
  std::int32_t position{0};
  std::string approach;
  bool fits_bounds{false};
  std::string plan;
  bool chosen{false};
  std::string reason;
};

auto read_rows(cms::db::connection& database, std::int64_t job) -> std::vector<StoredRow> {
  auto rows = std::vector<StoredRow>{};
  for (const auto& row : database(sqlpp::select(cms::db::generationCandidates.position,
                                               cms::db::generationCandidates.approach,
                                               cms::db::generationCandidates.fitsBounds,
                                               cms::db::generationCandidates.plan,
                                               cms::db::generationCandidates.chosen,
                                               cms::db::generationCandidates.reason)
                                       .from(cms::db::generationCandidates)
                                       .where(cms::db::generationCandidates.jobId == job)
                                       .order_by(cms::db::generationCandidates.position.asc()))) {
    rows.push_back(StoredRow{.position = static_cast<std::int32_t>(row.position),
                             .approach = std::string{row.approach},
                             .fits_bounds = row.fitsBounds,
                             .plan = std::string{row.plan},
                             .chosen = row.chosen,
                             .reason = std::string{row.reason}});
  }
  return rows;
}

}  // namespace

TEST_CASE("the in-memory sink keeps what a step recorded, against the job it was given", "[generate]") {
  auto sink = cms::generate::MemoryCandidateSink{};
  const auto judgement = plan_and_decide();

  const auto written = sink.record(17, judgement.records);

  REQUIRE(written.has_value());
  CHECK(*written == 4);
  CHECK(sink.job_id() == 17);
  CHECK(sink.rows().size() == 4);
  CHECK(sink.rows()[1].chosen);
  CHECK(sink.rows()[1].reason == "the cheapest plan that fits");
}

TEST_CASE("an empty decision is refused rather than written as nothing", "[generate]") {
  auto sink = cms::generate::MemoryCandidateSink{};

  const auto written = sink.record(1, {});

  REQUIRE(!written.has_value());
  CHECK(written.error().message.find("no candidate") != std::string::npos);
}

TEST_CASE("the plan stage's rows land in generation_candidates", "[generate][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();
  const auto job = cms::generate::test::prepare_job(database, "pipeline-plan-probe");
  const auto judgement = plan_and_decide();
  auto sink = cms::generate::SqlCandidateSink{*cms::generate::test::live_client()};

  const auto written = sink.record(job, judgement.records);

  REQUIRE(written.has_value());
  CHECK(*written == 4);
  CHECK(read_rows(database, job).size() == 4);
}

TEST_CASE("a stored row carries the plan, the fit and the reason", "[generate][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();
  const auto job = cms::generate::test::prepare_job(database, "pipeline-row-probe");
  auto sink = cms::generate::SqlCandidateSink{*cms::generate::test::live_client()};

  REQUIRE(sink.record(job, plan_and_decide().records).has_value());
  const auto rows = read_rows(database, job);

  REQUIRE(rows.size() == 4);
  CHECK(rows[0].position == 1);
  CHECK(rows[3].position == 4);
  CHECK(rows[1].approach == "segment tree");
  CHECK(rows[1].fits_bounds);
  CHECK(rows[1].chosen);
  CHECK(rows[1].reason == "the cheapest plan that fits");
  CHECK(rows[0].reason == "slower than the bound allows");
  CHECK(rows[2].reason == "more machinery than the task needs");
  CHECK(rows[3].reason == "no simpler proof");
  CHECK(rows[1].plan.find("\"approach\"") != std::string::npos);
  CHECK(rows[1].plan.find("segment tree") != std::string::npos);
}

TEST_CASE("a decision the database refuses leaves no row behind", "[generate][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();
  const auto job = cms::generate::test::prepare_job(database, "pipeline-rollback-probe");
  auto records = plan_and_decide().records;
  records[2].position = 1;
  auto sink = cms::generate::SqlCandidateSink{*cms::generate::test::live_client()};

  const auto written = sink.record(job, records);

  REQUIRE(!written.has_value());
  CHECK(read_rows(database, job).empty());
}

namespace {

/// Runs one statement on the live database and returns the refusal, empty when it ran. A decision's own check is
/// deferred to commit, so the helper forces pending constraints to fire before it asks: a half-written decision
/// then fails here with the rule's own sentence rather than at a commit whose message would be generic.
auto refused_now(std::string_view sql, std::vector<cms::db::bound_value> parameters) -> std::string {
  auto transaction = cms::db::Transaction{*cms::generate::test::live_client()};
  auto& connection = transaction.connection();
  try {
    connection.execute_text(std::string{sql}, std::move(parameters));
    connection.execute_text("SET CONSTRAINTS ALL IMMEDIATE", {});
  } catch (const std::exception& failure) {
    return std::string{failure.what()};
  }
  return {};
}

/// One decision row, with the facts the record refuses varied by the caller.
constexpr std::string_view ROW_SQL =
    "INSERT INTO generation_candidates (job_id, position, approach, intended_complexity, fits_bounds, plan, "
    "chosen, reason) VALUES ";

}  // namespace

TEST_CASE("a decision recorded twice is a conflict, not an outage", "[generate][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();
  const auto job = cms::generate::test::prepare_job(database, "pipeline-replay-probe");
  auto sink = cms::generate::SqlCandidateSink{*cms::generate::test::live_client()};
  REQUIRE(sink.record(job, plan_and_decide().records).has_value());

  const auto replay = sink.record(job, plan_and_decide().records);

  REQUIRE(!replay.has_value());
  CHECK(replay.error().code == cms::ErrorCode::conflict);
  CHECK(replay.error().message.find("already records a decision at position") != std::string::npos);
  CHECK(read_rows(database, job).size() == 4);
}

TEST_CASE("a decision with two chosen candidates is refused", "[generate][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();
  const auto job = cms::generate::test::prepare_job(database, "pipeline-two-chosen-probe");

  const auto refusal = refused_now(
      std::string{ROW_SQL} + "($1, 1, 'a', 'O(n)', true, '{\"k\":1}', true, 'won'), "
                             "($1, 2, 'b', 'O(n)', true, '{\"k\":2}', true, 'also won')",
      {job});

  CHECK(refusal.find("a decision chooses exactly one") != std::string::npos);
}

TEST_CASE("a position the decision does not have is refused", "[generate][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();
  const auto job = cms::generate::test::prepare_job(database, "pipeline-gap-probe");

  const auto refusal = refused_now(
      std::string{ROW_SQL} + "($1, 1, 'a', 'O(n)', true, '{\"k\":1}', true, 'won'), "
                             "($1, 3, 'c', 'O(n)', true, '{\"k\":3}', false, 'lost')",
      {job});

  CHECK(refusal.find("position 3 is not a candidate") != std::string::npos);
}

TEST_CASE("a chosen candidate with a blank reason is refused", "[generate][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();
  const auto job = cms::generate::test::prepare_job(database, "pipeline-blank-reason-probe");

  const auto refusal =
      refused_now(std::string{ROW_SQL} + "($1, 1, 'a', 'O(n)', true, '{\"k\":1}', true, '   ')", {job});

  CHECK(refusal.find("generation_candidates_reason_check") != std::string::npos);
}

TEST_CASE("a candidate numbered below one is refused", "[generate][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();
  const auto job = cms::generate::test::prepare_job(database, "pipeline-zero-position-probe");

  const auto refusal = refused_now(
      std::string{ROW_SQL} + "($1, 0, 'a', 'O(n)', true, '{\"k\":1}', true, 'won')", {job});

  CHECK(refusal.find("generation_candidates_position_check") != std::string::npos);
}

TEST_CASE("two jobs for one task version are refused", "[generate][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();
  (void)cms::generate::test::prepare_job(database, "pipeline-one-job-probe");

  const auto refusal = refused_now(
      "INSERT INTO generation_jobs (task_id, task_version) VALUES ($1, 1)",
      {std::string{"pipeline-one-job-probe"}});

  CHECK(refusal.find("generation_jobs_task_version_unique") != std::string::npos);
}
