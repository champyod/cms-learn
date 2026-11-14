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
