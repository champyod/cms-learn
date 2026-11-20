#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <sqlpp23/sqlpp23.h>

#include <cms/db/connection.hpp>
#include <cms/db/tables.hpp>
#include <cms/db/transaction.hpp>
#include <cms/generate/artefact.hpp>
#include <cms/generate/candidates.hpp>
#include <cms/generate/certify.hpp>
#include <cms/generate/model.hpp>
#include <cms/generate/pipeline.hpp>
#include <cms/generate/solution.hpp>
#include <cms/generate/specification.hpp>
#include <cms/generate/statement.hpp>
#include <cms/generate/test_plan.hpp>

#include "live_database.hpp"
#include "memory_store.hpp"
#include "pipeline_fixture.hpp"

namespace {

constexpr auto RUN_TASK = "pipeline-run-probe";
constexpr auto REFERENCE_TASK = "pipeline-reference-probe";
constexpr auto REFUSAL_TASK = "pipeline-refusal-probe";
constexpr auto MISSING_TASK = "pipeline-missing-probe";
constexpr auto CERTIFICATE_TASK = "pipeline-certificate-probe";
constexpr auto BRIEF_TASK = "pipeline-brief-probe";
constexpr auto WORKER = "worker-probe";
constexpr std::string_view COMMIT = "a1b2c3d4e5f60718293a4b5c6d7e8f9012345678";
/// The task's own limits, equal to what step 3 derives from O(n log n) at the task's bound: the certifier refuses a
/// specification whose derived limits are not the task's, so the fixture states them where the task row carries them.
constexpr std::int64_t TASK_TIME_MS = 100;
constexpr std::int64_t TASK_MEMORY_MB = 16;

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

constexpr std::string_view SUM_REFERENCE = R"(#include <cstdio>
int main() {
  int count = 0;
  if (std::scanf("%d", &count) != 1) return 1;
  long long total = 0;
  for (int index = 0; index < count; ++index) {
    long long value = 0;
    if (std::scanf("%lld", &value) != 1) return 1;
    total = total + value;
  }
  std::printf("%lld\n", total);
  return 0;
}
)";

/// The reference that is right only on the smallest case: the differential finds it at the subtask's bound.
constexpr std::string_view WRONG_REFERENCE = R"(#include <cstdio>
int main() {
  int count = 0;
  if (std::scanf("%d", &count) != 1) return 1;
  long long total = 0;
  for (int index = 0; index < count; ++index) {
    long long value = 0;
    if (std::scanf("%lld", &value) != 1) return 1;
    total = total + value;
  }
  std::printf("%lld\n", count == 1 ? total : 42LL);
  return 0;
}
)";

/// The first subtask's solution: right up to its own bound, wrong above it, which is where the second subtask lives.
constexpr std::string_view FIRST_PARTIAL = R"(#include <cstdio>
int main() {
  int count = 0;
  if (std::scanf("%d", &count) != 1) return 1;
  long long total = 0;
  for (int index = 0; index < count; ++index) {
    long long value = 0;
    if (std::scanf("%lld", &value) != 1) return 1;
    total = total + value;
  }
  std::printf("%lld\n", count <= 100 ? total : 42LL);
  return 0;
}
)";

/// The second subtask's solution: right from its own floor up, wrong below it, which is where the first subtask lives.
constexpr std::string_view SECOND_PARTIAL = R"(#include <cstdio>
int main() {
  int count = 0;
  if (std::scanf("%d", &count) != 1) return 1;
  long long total = 0;
  for (int index = 0; index < count; ++index) {
    long long value = 0;
    if (std::scanf("%lld", &value) != 1) return 1;
    total = total + value;
  }
  std::printf("%lld\n", count >= 1000 ? total : 42LL);
  return 0;
}
)";

/// A generator implementing the contract: stdin "<seed> <n>", stdout "n" then n values in the declared bounds.
constexpr std::string_view GENERATOR = R"(#include <cstdio>
int main() {
  long long seed = 0;
  long long n = 0;
  if (std::scanf("%lld %lld", &seed, &n) != 2) return 1;
  std::printf("%lld\n", n);
  for (long long index = 0; index < n; ++index) {
    std::printf("0%c", index + 1 == n ? '\n' : ' ');
  }
  return 0;
}
)";

/// A checker implementing the contract: argv expected, answer; exit 0 when the tokens agree.
constexpr std::string_view CHECKER = R"(#include <fstream>
#include <string>
int main(int argc, char** argv) {
  if (argc < 3) return 2;
  std::ifstream expected_file(argv[1]);
  std::ifstream answer_file(argv[2]);
  std::string expected;
  std::string answer;
  while (expected_file >> expected && answer_file >> answer) {
    if (expected != answer) return 1;
  }
  if (expected_file >> expected) return 1;
  if (answer_file >> answer) return 1;
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

/// @return One shard's answer: the three facts step 2 reads, and the trap sentence ADR 0036 §3 makes the plan carry.
auto planner_answer(std::string_view approach) -> std::string {
  return "{\"approach\": \"" + std::string{approach} +
         "\", \"intended_complexity\": \"O(n log n)\", \"fits_bounds\": true, "
         "\"trap\": \"the reader is meant to believe a scan of every range is fast enough\", "
         "\"constraints\": {\"n\": 200000}}";
}

auto judge_answer() -> std::string {
  return "{\"chosen\": 2, \"reason\": \"the cheapest idea that fits\", \"rejected\": ["
         "{\"position\": 1, \"reason\": \"its cost does not fit the bounds\"}, "
         "{\"position\": 3, \"reason\": \"more machinery than the task needs\"}, "
         "{\"position\": 4, \"reason\": \"a worse constant, no simpler proof\"}]}";
}

auto specification_answer() -> std::string {
  return R"({"input": "n, then n integers", "output": "their sum", "input_shape": "n_then_values",)"
         R"("bounds": {"min_n": 1, "max_n": 200000, "value_min": -1000000000, "value_max": 1000000000},)"
         R"("subtasks": [{"position": 1, "max_n": 100}, )"
         R"({"position": 2, "min_n": 1000, "max_n": 200000}], "oracle": )" +
         json_string(SUM_ORACLE) + "}";
}

auto statement_answer(std::string_view locale) -> std::string {
  auto json = std::string{"{\"title\": \"Range Sum Queries\""};
  for (const auto key : {"task", "input", "output", "constraints", "example", "scoring"}) {
    json += ", ";
    json += json_string(key);
    json += ": ";
    json += json_string(std::string{"The statement's "} + key + " section, written for " +
                        std::string{locale} + ".");
  }
  json += '}';
  return json;
}

/// @return A recording of one whole job: four shards, the decision, step 3, one statement, step 5's two programs, and
/// step 6's two programs.
auto whole_job_recording(std::string_view reference = SUM_REFERENCE) -> cms::generate::RecordedClient {
  return cms::generate::RecordedClient{
      "deployer-model",
      "prompt-v1",
      7,
      {planner_answer("prefix sums"), planner_answer("segment tree"), planner_answer("sqrt decomposition"),
       planner_answer("sparse table"), judge_answer(), specification_answer(), statement_answer("en"),
       std::string{reference}, std::string{FIRST_PARTIAL}, std::string{SECOND_PARTIAL}, std::string{GENERATOR},
       std::string{CHECKER}}};
}

/// @brief A certifier that answers what a certificate would say, without running the three methods again.
///
/// The certifier's own verdicts are certify_test's evidence: its memory method measures the child's peak RSS, which
/// under a sanitizer-built test binary includes the pages the child inherits from this process, so a small derived
/// memory limit is measured against the harness rather than against the program. The runner's rule is what this file
/// proves: the ending follows the certificate, whichever way it reads.
class StubCertifier final : public cms::generate::Certifier {
 public:
  StubCertifier(std::string_view task_id, cms::generate::VerificationOutcome outcome,
                std::string check = {})
      : task_id_(task_id), outcome_(outcome), check_(std::move(check)) {}

  [[nodiscard]] auto certify(std::int64_t job_id) -> cms::Result<cms::generate::Certificate> override {
    auto checks = std::vector<std::string>{"the evidence was read back through its digests"};
    if (!check_.empty()) {
      checks.push_back(check_);
    }
    return cms::generate::Certificate{.job_id = job_id,
                                      .task_id = std::string{task_id_},
                                      .task_version = 1,
                                      .outcome = outcome_,
                                      .commit_id = std::string{COMMIT},
                                      .checks = std::move(checks),
                                      .artefacts = {}};
  }

 private:
  std::string task_id_;
  cms::generate::VerificationOutcome outcome_;
  std::string check_;
};

/// @brief Leaves the case's job the only claimable one.
///
/// The live cases share one database and a run claims the oldest queued job, so the jobs earlier cases left queued
/// are ended first — the same way a worker that finished them would — or this case's run would take one of theirs.
auto drain_queue(cms::db::connection& database) -> void {
  database.execute_text("UPDATE generation_jobs SET state = 'failed' WHERE state = 'queued'", {});
}

/// @brief Sets the actor the audit triggers read, the way every live fixture that writes content must.
auto actor(cms::db::connection& database) -> void {
  database.execute_text(
      "INSERT INTO admin_accounts (id, handle, status) "
      "VALUES ('a0000001-1111-4111-8111-0000000000c2'::uuid, 'pipeline_probe', 'active') "
      "ON CONFLICT (id) DO NOTHING",
      {});
  database.execute_text("SELECT set_config('cms.actor_id', $1, true)",
                        {std::string{"a0000001-1111-4111-8111-0000000000c2"}});
  database.execute_text("SELECT set_config('cms.actor_kind', 'admin', true)", {});
  database.execute_text("SELECT set_config('cms.invoked_by', 'pipeline_test', true)", {});
}

/// @brief Writes one draft task with the limits step 3 derives, its topic, statement and subtasks, and one queued job.
///
/// Everything that writes content runs in one transaction with this fixture's actor, because the audit triggers read
/// that setting only for the transaction that carries it.
/// @return The job's id.
auto seed_job(std::string_view task_id) -> std::int64_t {
  auto transaction = cms::db::Transaction{*cms::generate::test::live_client()};
  auto& connection = transaction.connection();
  actor(connection);
  const auto job = cms::generate::test::prepare_job(connection, task_id);
  connection.execute_text(
      "INSERT INTO topics (id, label_en, label_th) VALUES ($1, $2, $3) ON CONFLICT (id) DO NOTHING",
      {"prefix-sums", "Prefix sums", "ผลรวมสะสม"});
  connection.execute_text(
      "UPDATE tasks SET time_limit_ms = $3, memory_limit_mb = $4 WHERE id = $1 AND version = $2",
      {std::string{task_id}, std::int64_t{1}, TASK_TIME_MS, TASK_MEMORY_MB});
  // The teacher's request lives on the job row (ADR 0036 §2); step 4 refuses a job that stored none.
  connection.execute_text("UPDATE generation_jobs SET brief = $2, description = $3 WHERE id = $1",
                          {job, "a sequence and its range sums", "range sums probe"});
  connection.execute_text("INSERT INTO task_topics (task_id, task_version, topic) VALUES ($1, 1, $2)",
                          {std::string{task_id}, "prefix-sums"});
  // A task carries subtasks only when its type scores by subtask, so the fixture declares that mode the way the seeder
  // does: one type, the field definitions it names, and the value the form would record.
  connection.execute_text(
      "INSERT INTO task_types (id, title_en, title_th, position) VALUES ('batch', 'Batch', 'ชุด', 1) "
      "ON CONFLICT (id) DO NOTHING",
      {});
  connection.execute_text(
      "INSERT INTO task_field_defs (task_type_id, key, kind, label_en, label_th, required, choices, position) "
      "VALUES ('batch', 'score_mode', 'choice', 'Score mode', 'วิธีให้คะแนน', true, "
      "'[\"per_case\", \"all_or_nothing\", \"subtasks\"]'::jsonb, 1) ON CONFLICT DO NOTHING",
      {});
  connection.execute_text("UPDATE tasks SET task_type = 'batch' WHERE id = $1 AND version = 1",
                          {std::string{task_id}});
  connection.execute_text(
      "INSERT INTO task_field_values (task_id, task_version, field_key, value) "
      "VALUES ($1, 1, 'score_mode', '\"subtasks\"'::jsonb)",
      {std::string{task_id}});
  // The task's own subtasks, which step 3's specification narrows and the certifier compares the solutions against.
  connection.execute_text(
      "INSERT INTO task_subtasks (task_id, task_version, position, points, max_n) VALUES ($1, 1, 1, 40, 100)",
      {std::string{task_id}});
  connection.execute_text(
      "INSERT INTO task_subtasks (task_id, task_version, position, points, min_n, max_n) "
      "VALUES ($1, 1, 2, 60, 1000, 200000)",
      {std::string{task_id}});
  connection.execute_text(
      "INSERT INTO task_statements (task_id, task_version, locale, body) VALUES ($1, 1, $2, $3)",
      {std::string{task_id}, "en", "# Probe\n\nBody."});
  transaction.commit();
  return job;
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

/// @return The task's verification, which only the certifier's certificate may move.
auto verification_of(cms::db::connection& database, std::string_view task_id) -> std::string {
  auto verification = std::string{};
  for (const auto& row : database(sqlpp::select(cms::db::tasks.verification)
                                      .from(cms::db::tasks)
                                      .where(cms::db::tasks.id == std::string{task_id}))) {
    verification = std::string{row.verification};
  }
  return verification;
}

/// @return The refusal rows the job's record holds, as (step, text).
auto refusals_of(cms::db::connection& database, std::int64_t job_id)
    -> std::vector<std::pair<std::int64_t, std::string>> {
  auto rows = std::vector<std::pair<std::int64_t, std::string>>{};
  for (const auto& row : database(sqlpp::select(cms::db::generationArtefacts.kind, cms::db::generationArtefacts.step,
                                                cms::db::generationArtefacts.inlineText)
                                      .from(cms::db::generationArtefacts)
                                      .where(cms::db::generationArtefacts.jobId == job_id))) {
    if (row.kind == "refusal") {
      rows.emplace_back(static_cast<std::int64_t>(row.step), std::string{*row.inlineText});
    }
  }
  return rows;
}

/// @brief The sinks and the certifier one run writes through, as the binary builds them.
struct Wired {
  Wired(cms::generate::Certifier& certifier, cms::generate::test::MemoryStore& store)
      : candidates{*cms::generate::test::live_client()},
        specifications{*cms::generate::test::live_client(), store},
        artefacts{*cms::generate::test::live_client(), store},
        statements{*cms::generate::test::live_client(), store},
        solutions{*cms::generate::test::live_client(), store},
        test_plans{*cms::generate::test::live_client(), store},
        ports{.client = *cms::generate::test::live_client(),
              .store = store,
              .candidates = candidates,
              .specifications = specifications,
              .artefacts = artefacts,
              .statements = statements,
              .solutions = solutions,
              .test_plans = test_plans,
              .certifier = certifier} {}

  cms::generate::SqlCandidateSink candidates;
  cms::generate::SqlSpecificationSink specifications;
  cms::generate::SqlArtefactSink artefacts;
  cms::generate::SqlStatementSink statements;
  cms::generate::SqlSolutionSink solutions;
  cms::generate::SqlTestPlanSink test_plans;
  cms::generate::PipelinePorts ports;
};

/// @return The declared locales, which the caller supplies because ADR 0031 §2 keeps a language's name out of the step.
auto declared_locales() -> std::vector<cms::generate::LocaleDeclaration> {
  return {cms::generate::LocaleDeclaration{.tag = "en", .name = "English"}};
}

/// @return The vocabulary the technique's names are read from: the content tree that ships with the image.
auto vocabulary() -> std::filesystem::path {
  return std::filesystem::path{CMS_CONTENT_DIR} / "topics.json";
}

auto facts() -> cms::generate::RunnerFacts {
  return cms::generate::RunnerFacts{.vocabulary = vocabulary(), .locales = declared_locales()};
}

}  // namespace

TEST_CASE("a queued job runs every step and ends done on a verified certificate", "[generate][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();
  drain_queue(database);
  const auto job = seed_job(RUN_TASK);
  auto store = cms::generate::test::MemoryStore{};
  auto certifier = StubCertifier{RUN_TASK, cms::generate::VerificationOutcome::verified};
  auto wired = Wired{certifier, store};
  auto client = whole_job_recording();

  const auto report = cms::generate::run_pipeline(client, database, WORKER, wired.ports, facts());

  if (!report) {
    UNSCOPED_INFO(report.error().message);
  }
  REQUIRE(report.has_value());
  CHECK(report->job_id == job);
  CHECK(report->state == "done");
  CHECK(report->steps.size() == 7);
  // Four shards, the decision, the specification, the statement, the reference, two partials, two test programs.
  CHECK(client.prompts().size() == 12);

  const auto [state, holder, attempts] = job_row(database, job);
  CHECK(state == "done");
  CHECK(holder.empty());
  CHECK(attempts == 1);

  auto candidate_rows = std::int64_t{0};
  auto chosen = std::int64_t{0};
  for (const auto& row : database(sqlpp::select(cms::db::generationCandidates.chosen)
                                      .from(cms::db::generationCandidates)
                                      .where(cms::db::generationCandidates.jobId == job))) {
    ++candidate_rows;
    chosen += row.chosen ? 1 : 0;
  }
  CHECK(candidate_rows == 4);
  CHECK(chosen == 1);

  CHECK(refusals_of(database, job).empty());
}

TEST_CASE("a wrong reference ends the job failed at step 5", "[generate][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();
  drain_queue(database);
  const auto job = seed_job(REFERENCE_TASK);
  auto store = cms::generate::test::MemoryStore{};
  auto certifier = cms::generate::SqlCertifier{*cms::generate::test::live_client(), store, COMMIT};
  auto wired = Wired{certifier, store};
  auto client = whole_job_recording(WRONG_REFERENCE);

  const auto report = cms::generate::run_pipeline(client, database, WORKER, wired.ports, facts());

  if (!report) {
    UNSCOPED_INFO(report.error().message);
  }
  REQUIRE(!report.has_value());
  CHECK(report.error().message.find("step 5") != std::string::npos);

  const auto [state, holder, attempts] = job_row(database, job);
  CHECK(state == "failed");
  CHECK(holder.empty());
  CHECK(attempts == 1);
  CHECK(verification_of(database, REFERENCE_TASK) == "pending");

  const auto refusals = refusals_of(database, job);
  REQUIRE(refusals.size() == 1);
  CHECK(refusals.front().first == 5);
}

TEST_CASE("a job with no step 3 artefact is refused by name", "[generate][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();
  drain_queue(database);
  const auto job = seed_job(MISSING_TASK);
  const auto claimed = cms::generate::claim_job(database, WORKER);
  REQUIRE(claimed.has_value());
  REQUIRE(claimed->has_value());
  auto store = cms::generate::test::MemoryStore{};
  auto certifier = cms::generate::SqlCertifier{*cms::generate::test::live_client(), store, COMMIT};
  auto wired = Wired{certifier, store};
  auto client = whole_job_recording();

  const auto report = cms::generate::run_second_half(client, database, WORKER, **claimed, wired.ports, facts());

  REQUIRE(!report.has_value());
  CHECK(report.error().message.find("step 4") != std::string::npos);
  CHECK(report.error().message.find("step 3") != std::string::npos);
  CHECK(report.error().message.find("specification") != std::string::npos);
  // Nothing was recorded for step 4, and the job did not stay running.
  const auto [state, holder, attempts] = job_row(database, job);
  CHECK(state == "failed");
  CHECK(holder.empty());
  auto statement_rows = std::int64_t{0};
  for (const auto& row : database(sqlpp::select(cms::db::generationArtefacts.step, cms::db::generationArtefacts.kind)
                                      .from(cms::db::generationArtefacts)
                                      .where(cms::db::generationArtefacts.jobId == job))) {
    statement_rows += row.step == 4 && row.kind != "refusal" ? 1 : 0;
  }
  CHECK(statement_rows == 0);
  CHECK(client.prompts().empty());
}

TEST_CASE("a certificate that is not verified ends the job failed", "[generate][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();
  drain_queue(database);
  const auto job = seed_job(CERTIFICATE_TASK);
  auto store = cms::generate::test::MemoryStore{};
  auto certifier = StubCertifier{CERTIFICATE_TASK, cms::generate::VerificationOutcome::failed,
                                 "the reference disagrees with the oracle at n = 100"};
  auto wired = Wired{certifier, store};
  auto client = whole_job_recording();

  const auto report = cms::generate::run_pipeline(client, database, WORKER, wired.ports, facts());

  if (!report) {
    UNSCOPED_INFO(report.error().message);
  }
  REQUIRE(!report.has_value());
  CHECK(report.error().message.find("step 7") != std::string::npos);
  CHECK(report.error().message.find("disagrees with the oracle") != std::string::npos);

  const auto [state, holder, attempts] = job_row(database, job);
  CHECK(state == "failed");
  CHECK(holder.empty());
  CHECK(verification_of(database, CERTIFICATE_TASK) == "pending");
  const auto refusals = refusals_of(database, job);
  REQUIRE(refusals.size() == 1);
  CHECK(refusals.front().first == 7);
  CHECK(refusals.front().second.find("disagrees with the oracle") != std::string::npos);
}

TEST_CASE("a step's refusal ends the job failed and is recorded on the job", "[generate][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();
  drain_queue(database);
  const auto job = seed_job(REFUSAL_TASK);
  auto store = cms::generate::test::MemoryStore{};
  auto certifier = cms::generate::SqlCertifier{*cms::generate::test::live_client(), store, COMMIT};
  auto wired = Wired{certifier, store};
  // The first shard answers with a plan that names no approach, which step 1 refuses by name.
  auto client = cms::generate::RecordedClient{
      "deployer-model", "prompt-v1", 7,
      {"{\"approach\": \"\", \"intended_complexity\": \"O(n log n)\", \"fits_bounds\": true, "
       "\"trap\": \"a scan is fast enough\"}"}};

  const auto report = cms::generate::run_pipeline(client, database, WORKER, wired.ports, facts());

  if (report) {
    UNSCOPED_INFO("the run returned job " + std::to_string(report->job_id) + " state " + report->state);
  }
  REQUIRE(!report.has_value());
  CHECK(report.error().message.find("step 1") != std::string::npos);

  const auto [state, holder, attempts] = job_row(database, job);
  CHECK(state == "failed");
  CHECK(holder.empty());
  const auto refusals = refusals_of(database, job);
  REQUIRE(refusals.size() == 1);
  CHECK(refusals.front().first == 1);
  CHECK(refusals.front().second.find("missing or has wrong: approach") != std::string::npos);
}

TEST_CASE("the brief names the task's own difficulty, topics and technique", "[generate][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();
  const auto job = seed_job(BRIEF_TASK);
  auto claim = cms::generate::JobClaim{
      .id = job, .task_id = BRIEF_TASK, .task_version = 1, .attempts = 1, .worker = WORKER};

  const auto brief = cms::generate::load_brief(database, claim);

  REQUIRE(brief.has_value());
  CHECK(brief->technique == "prefix-sums");
  CHECK(brief->difficulty == "900");
  CHECK(brief->topics == std::vector<std::string>{"prefix-sums"});
  CHECK(brief->locales == std::vector<std::string>{"en"});
}
