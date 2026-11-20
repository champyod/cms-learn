#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <sqlpp23/sqlpp23.h>

#include <cms/db/connection.hpp>
#include <cms/db/tables.hpp>
#include <cms/generate/artefact.hpp>
#include <cms/generate/candidates.hpp>
#include <cms/generate/model.hpp>
#include <cms/generate/pipeline.hpp>
#include <cms/generate/specification.hpp>

#include "live_database.hpp"
#include "memory_store.hpp"
#include "pipeline_fixture.hpp"

namespace {

constexpr auto RUN_TASK = "pipeline-run-probe";
constexpr auto REFUSAL_TASK = "pipeline-refusal-probe";
constexpr auto BRIEF_TASK = "pipeline-brief-probe";
constexpr auto WORKER = "worker-probe";

/// A sum, so step 3's oracle is a program the machine can actually compile and run.
constexpr std::string_view SUM_ORACLE = R"(#include <iostream>
int main() {
  long long n = 0;
  std::cin >> n;
  long long total = 0;
  for (long long index = 0; index < n; ++index) {
    long long value = 0;
    std::cin >> value;
    total += value;
  }
  std::cout << total << "\n";
  return 0;
}
)";

/// @return @p text as the body of a JSON string, so a program's newlines do not break the answer.
auto json_string(std::string_view text) -> std::string {
  auto out = std::string{"\""};
  for (const auto character : text) {
    if (character == '\n') {
      out += "\\n";
      continue;
    }
    if (character == '"' || character == '\\') {
      out.push_back('\\');
    }
    out.push_back(character);
  }
  out.push_back('"');
  return out;
}

auto planner_answer(std::string_view approach) -> std::string {
  return "{\"approach\": \"" + std::string{approach} +
         "\", \"intended_complexity\": \"O(n log n)\", \"fits_bounds\": true, "
         "\"constraints\": {\"n\": 200000}}";
}

auto judge_answer() -> std::string {
  return "{\"chosen\": 2, \"reason\": \"the cheapest idea that fits\", \"rejected\": ["
         "{\"position\": 1, \"reason\": \"its cost does not fit the bounds\"}, "
         "{\"position\": 3, \"reason\": \"more machinery than the task needs\"}, "
         "{\"position\": 4, \"reason\": \"a worse constant, no simpler proof\"}]}";
}

/// @return A plan whose approach is blank: step 1 refuses it by name.
auto planner_answer_without_approach() -> std::string {
  return R"plan({"approach": "", "intended_complexity": "O(n log n)", "fits_bounds": true})plan";
}

auto specification_answer() -> std::string {
  return R"({"input": "n, then n integers", "output": "their sum", "input_shape": "n_then_values",)"
         R"("bounds": {"min_n": 1, "max_n": 200000, "value_min": -1000000000, "value_max": 1000000000},)"
         R"("subtasks": [{"position": 1, "max_n": 100}], "oracle": )" +
         json_string(SUM_ORACLE) + "}";
}

/// @return A recording of one whole job: four shards, the decision, and step 3's answer.
auto whole_job_recording() -> cms::generate::RecordedClient {
  return cms::generate::RecordedClient{"deployer-model",
                                       "prompt-v1",
                                       7,
                                       {planner_answer("prefix sums"), planner_answer("segment tree"),
                                        planner_answer("sqrt decomposition"), planner_answer("sparse table"),
                                        judge_answer(), specification_answer()}};
}

/// @brief Writes one draft task, its topic and statement, and one queued job for it.
/// @param task_id The task the job names; each case uses its own, because the live cases share one database.
/// @return The job's id.
auto seed_job(cms::db::connection& database, std::string_view task_id) -> std::int64_t {
  database.execute_text(
      "INSERT INTO topics (id, label_en, label_th) VALUES ($1, $2, $3) ON CONFLICT (id) DO NOTHING",
      {"prefix-sums", "Prefix sums", "ผลรวมสะสม"});
  const auto job = cms::generate::test::prepare_job(database, task_id);
  database.execute_text("INSERT INTO task_topics (task_id, task_version, topic) VALUES ($1, 1, $2)",
                        {std::string{task_id}, "prefix-sums"});
  database.execute_text(
      "INSERT INTO task_statements (task_id, task_version, locale, body) VALUES ($1, 1, $2, $3)",
      {std::string{task_id}, "en", "# Probe\n\nBody."});
  return job;
}

/// @brief Leaves the case's job the only claimable one.
///
/// The live cases share one database and a run claims the oldest queued job, so the jobs earlier cases left queued
/// are ended first — the same way a worker that finished them would — or this case's run would take one of theirs.
auto drain_queue(cms::db::connection& database) -> void {
  database.execute_text("UPDATE generation_jobs SET state = 'failed' WHERE state = 'queued'", {});
}

/// @return The job's state, holder and attempt count as the record holds them.
auto job_row(cms::db::connection& database, std::int64_t job_id)
    -> std::tuple<std::string, std::string, std::int64_t> {
  auto state = std::string{};
  auto holder = std::string{};
  auto attempts = std::int64_t{0};
  for (const auto& row : database(sqlpp::select(cms::db::generationJobs.state, cms::db::generationJobs.claimedBy,
                                                cms::db::generationJobs.attempts)
                                      .from(cms::db::generationJobs)
                                      .where(cms::db::generationJobs.id == job_id))) {
    state = std::string{row.state};
    holder = row.claimedBy.has_value() ? std::string{*row.claimedBy} : std::string{};
    attempts = static_cast<std::int64_t>(row.attempts);
  }
  return {state, holder, attempts};
}

}  // namespace

TEST_CASE("a queued job runs steps 1 to 3 and ends awaiting evidence", "[generate][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();
  drain_queue(database);
  const auto job = seed_job(database, RUN_TASK);
  auto store = cms::generate::test::MemoryStore{};
  auto candidates = cms::generate::SqlCandidateSink{*cms::generate::test::live_client()};
  auto specifications = cms::generate::SqlSpecificationSink{*cms::generate::test::live_client(), store};
  auto artefacts = cms::generate::SqlArtefactSink{*cms::generate::test::live_client(), store};
  auto client = whole_job_recording();

  const auto report = cms::generate::run_pipeline(client, database, WORKER, candidates, specifications, artefacts);

  if (!report) {
    UNSCOPED_INFO(report.error().message);
  }
  REQUIRE(report.has_value());
  CHECK(report->job_id == job);
  CHECK(report->state == "awaiting-evidence");
  CHECK(report->steps.size() == 3);
  // One call per shard, one for the decision, one for the specification: the steps ran in order and nothing else.
  CHECK(client.prompts().size() == 6);

  const auto [state, holder, attempts] = job_row(database, job);
  CHECK(state == "awaiting-evidence");
  CHECK(holder.empty());
  CHECK(attempts == 1);

  auto chosen = std::int64_t{0};
  auto candidate_rows = std::int64_t{0};
  for (const auto& row : database(sqlpp::select(cms::db::generationCandidates.chosen)
                                      .from(cms::db::generationCandidates)
                                      .where(cms::db::generationCandidates.jobId == job))) {
    ++candidate_rows;
    chosen += row.chosen ? 1 : 0;
  }
  CHECK(candidate_rows == 4);
  CHECK(chosen == 1);

  auto specification_rows = std::int64_t{0};
  for (const auto& row : database(sqlpp::select(cms::db::generationSpecifications.jobId)
                                      .from(cms::db::generationSpecifications)
                                      .where(cms::db::generationSpecifications.jobId == job))) {
    (void)row;
    ++specification_rows;
  }
  CHECK(specification_rows == 1);

  auto oracle_rows = std::int64_t{0};
  for (const auto& row : database(sqlpp::select(cms::db::generationArtefacts.kind)
                                      .from(cms::db::generationArtefacts)
                                      .where(cms::db::generationArtefacts.jobId == job))) {
    if (row.kind == "oracle") {
      ++oracle_rows;
    }
  }
  CHECK(oracle_rows == 1);
}

TEST_CASE("a step's refusal ends the job failed and is recorded on the job", "[generate][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();
  drain_queue(database);
  const auto job = seed_job(database, REFUSAL_TASK);
  auto store = cms::generate::test::MemoryStore{};
  auto candidates = cms::generate::SqlCandidateSink{*cms::generate::test::live_client()};
  auto specifications = cms::generate::SqlSpecificationSink{*cms::generate::test::live_client(), store};
  auto artefacts = cms::generate::SqlArtefactSink{*cms::generate::test::live_client(), store};
  // The first shard answers with a plan that names no approach, which step 1 refuses by name.
  auto client = cms::generate::RecordedClient{
      "deployer-model", "prompt-v1", 7, {planner_answer_without_approach()}};

  const auto report = cms::generate::run_pipeline(client, database, WORKER, candidates, specifications, artefacts);

  if (report) {
    UNSCOPED_INFO("the run succeeded; job " + std::to_string(report->job_id) + " ended " + report->state);
  } else {
    UNSCOPED_INFO(report.error().message);
  }
  REQUIRE(!report.has_value());
  CHECK(report.error().message.find("step 1") != std::string::npos);

  const auto [state, holder, attempts] = job_row(database, job);
  CHECK(state == "failed");
  CHECK(holder.empty());
  CHECK(attempts == 1);

  auto refusals = std::int64_t{0};
  auto refusal_step = std::int64_t{0};
  for (const auto& row : database(sqlpp::select(cms::db::generationArtefacts.kind,
                                               cms::db::generationArtefacts.step,
                                               cms::db::generationArtefacts.inlineText)
                                      .from(cms::db::generationArtefacts)
                                      .where(cms::db::generationArtefacts.jobId == job))) {
    if (row.kind != "refusal") {
      continue;
    }
    ++refusals;
    refusal_step = static_cast<std::int64_t>(row.step);
    CHECK(std::string{*row.inlineText}.find("missing or has wrong: approach") != std::string::npos);
  }
  CHECK(refusals == 1);
  CHECK(refusal_step == 1);
}

TEST_CASE("the brief names the task's own difficulty, topics and technique", "[generate][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();
  const auto job = seed_job(database, BRIEF_TASK);
  auto claim = cms::generate::JobClaim{.id = job, .task_id = BRIEF_TASK, .task_version = 1, .attempts = 1, .worker = WORKER};

  const auto brief = cms::generate::load_brief(database, claim);

  REQUIRE(brief.has_value());
  CHECK(brief->technique == "prefix-sums");
  CHECK(brief->difficulty == "900");
  CHECK(brief->topics == std::vector<std::string>{"prefix-sums"});
  CHECK(brief->locales == std::vector<std::string>{"en"});
}
