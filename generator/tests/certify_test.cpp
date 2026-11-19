// Step 7 is the only path to a verified task: it re-reads every artefact steps 3, 5 and 6 cited, re-runs the one proof
// that can be run cheaply (the oracle against the reference), and writes what it checked, the commit it checked and the
// digests it saw. Incomplete or stale evidence is refused and nothing is written.

#include <cms/generate/certify.hpp>
#include <cms/generate/specification.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <sqlpp23/sqlpp23.h>

#include <cms/db/connection.hpp>
#include <cms/db/tables.hpp>
#include <cms/db/transaction.hpp>
#include <cms/object_store.hpp>

#include "live_database.hpp"
#include "memory_store.hpp"

namespace {

constexpr std::string_view COMMIT = "a1b2c3d4e5f60718293a4b5c6d7e8f9012345678";
constexpr std::string_view TYPE_ID = "certify_type";

auto oracle_source() -> std::string {
  return R"(#include <cstdio>
int main() {
  int n = 0;
  if (std::scanf("%d", &n) != 1) return 1;
  long long sum = 0;
  for (int index = 0; index < n; ++index) {
    long long value = 0;
    if (std::scanf("%lld", &value) != 1) return 1;
    sum += value;
  }
  std::printf("%lld\n", sum);
  return 0;
}
)";
}

auto reference_source() -> std::string {
  return R"(#include <cstdio>
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
}

auto wrong_reference_source() -> std::string {
  return R"(#include <cstdio>
int main() {
  std::printf("42\n");
  return 0;
}
)";
}

auto actor(cms::db::connection& database) -> void {
  database.execute_text("INSERT INTO admin_accounts (id, handle, status) "
                        "VALUES ('a0000001-1111-4111-8111-0000000000c1'::uuid, 'certify_probe', 'active') "
                        "ON CONFLICT (id) DO NOTHING",
                        {});
  database.execute_text("SELECT set_config('cms.actor_id', $1, true)",
                        {std::string{"a0000001-1111-4111-8111-0000000000c1"}});
  database.execute_text("SELECT set_config('cms.actor_kind', 'admin', true)", {});
  database.execute_text("SELECT set_config('cms.invoked_by', 'certify_test', true)", {});
}

auto insert_artefact(cms::db::connection& database, std::int64_t job, std::int16_t step,
                     std::string_view kind, std::string_view key, std::string_view digest) -> std::int64_t {
  auto id = std::int64_t{0};
  database.execute_text(
      "INSERT INTO generation_artefacts (job_id, step, kind, object_key, digest, model_id, prompt_version, seed, "
      "call_index) VALUES ($1, $2, $3, $4, $5, 'm', 'v', 1, 0)",
      {job, static_cast<std::int64_t>(step), std::string{kind}, std::string{key}, std::string{digest}});
  for (const auto& row : database(sqlpp::select(cms::db::generationArtefacts.id)
                                      .from(cms::db::generationArtefacts)
                                      .order_by(cms::db::generationArtefacts.id.desc())
                                      .limit(1))) {
    id = row.id;
  }
  return id;
}

auto specification_json() -> std::string {
  return R"({"input":"n then n values","output":"their sum","input_shape":"n_then_values",)"
         R"("bounds":{"min_n":1,"max_n":10,"value_min":0,"value_max":9},)"
         R"("subtasks":[{"position":1,"constraint":"n <= 10"}]})";
}

/// One job whose evidence is complete: a task with one subtask, the specification row, the oracle, the reference, the
/// subtask's solution, both programs and the case set — every artefact written to a store that can be faulted later.
auto build_job(cms::db::connection& database, cms::generate::test::MemoryStore& store, std::string_view task_id,
               std::string_view reference, bool with_specification, std::int64_t spec_time_ms,
               std::int64_t spec_memory_mb) -> std::int64_t {
  actor(database);
  database.execute_text("INSERT INTO task_types (id, title_en, position) VALUES ($1, 'Probe', 903) "
                        "ON CONFLICT (id) DO NOTHING",
                        {std::string{TYPE_ID}});
  database.execute_text(
      "INSERT INTO task_field_defs (task_type_id, key, kind, label_en, choices, position) "
      "VALUES ($1, 'score_mode', 'choice', 'Score mode', '[\"per_case\",\"all_or_nothing\",\"subtasks\"]', 1) "
      "ON CONFLICT DO NOTHING",
      {std::string{TYPE_ID}});
  database.execute_text(
      "INSERT INTO tasks (id, version, task_type, status, title_en, title_th, difficulty, time_limit_ms, "
      "memory_limit_mb, points, checker, origin, verification, solution, content_hash) "
      "VALUES ($1, 1, $2, 'draft', 'Probe', 'ทดสอบ', 900, 1000, 256, 100, 'token', 'original', 'pending', '', $3)",
      {std::string{task_id}, std::string{TYPE_ID}, std::string(64, 'c')});
  database.execute_text(
      "INSERT INTO task_field_values (task_id, task_version, field_key, value) "
      "VALUES ($1, 1, 'score_mode', to_jsonb('subtasks'::text))",
      {std::string{task_id}});
  database.execute_text(
      "INSERT INTO task_subtasks (task_id, task_version, position, points, constraint_text) "
      "VALUES ($1, 1, 1, 100, 'n <= 10')",
      {std::string{task_id}});
  database.execute_text("INSERT INTO generation_jobs (task_id, task_version, state) VALUES ($1, 1, $2)",
                        {std::string{task_id}, std::string{"awaiting-evidence"}});
  auto job = std::int64_t{0};
  for (const auto& row : database(sqlpp::select(cms::db::generationJobs.id)
                                      .from(cms::db::generationJobs)
                                      .where(cms::db::generationJobs.taskId == std::string{task_id})
                                      .order_by(cms::db::generationJobs.id.desc())
                                      .limit(1))) {
    job = row.id;
  }

  const auto write = [&](std::string_view key, std::string_view bytes) {
    store.overwrite(key, bytes);
    return cms::store::digest_of(bytes);
  };
  const auto oracle_key = std::string{"certify/"} + std::string{task_id} + "/oracle.cpp";
  const auto reference_key = std::string{"certify/"} + std::string{task_id} + "/reference.cpp";
  const auto partial_key = std::string{"certify/"} + std::string{task_id} + "/partial.cpp";
  const auto generator_key = std::string{"certify/"} + std::string{task_id} + "/generator.cpp";
  const auto checker_key = std::string{"certify/"} + std::string{task_id} + "/checker.cpp";
  const auto cases_key = std::string{"certify/"} + std::string{task_id} + "/cases.json";

  const auto oracle = insert_artefact(database, job, 3, "oracle", oracle_key,
                                      write(oracle_key, oracle_source()));
  const auto reference_artefact = insert_artefact(database, job, 5, "reference", reference_key,
                                                  write(reference_key, reference));
  const auto partial = insert_artefact(database, job, 5, "subtask-solution", partial_key,
                                       write(partial_key, reference_source()));
  const auto generator = insert_artefact(database, job, 6, "generator", generator_key,
                                         write(generator_key, "int main() { return 0; }\n"));
  const auto checker = insert_artefact(database, job, 6, "checker", checker_key,
                                       write(checker_key, "int main() { return 0; }\n"));
  const auto cases = insert_artefact(database, job, 6, "cases", cases_key, write(cases_key, "[]"));

  database.execute_text(
      "INSERT INTO generation_solutions (job_id, position, artefact_id, proof) VALUES "
      "($1, 0, $2, 'the reference was checked'), ($1, 1, $3, 'the partial was checked')",
      {job, reference_artefact, partial});
  database.execute_text(
      "INSERT INTO generation_programs (job_id, role, artefact_id, proof) VALUES "
      "($1, 'generator', $2, 'deterministic'), ($1, 'checker', $3, 'planted wrong answer refused')",
      {job, generator, checker});
  database.execute_text(
      "INSERT INTO generation_cases (job_id, subtask_position, artefact_id, case_count, proof) "
      "VALUES ($1, 1, $2, 2, 'two cases')",
      {job, cases});

  if (with_specification) {
    database.execute_text(
        "INSERT INTO generation_specifications (job_id, specification, time_limit_ms, memory_limit_mb, derivation, "
        "answer_artefact_id, oracle_artefact_id) VALUES ($1, $2::jsonb, $3, $4, 'derived', $5, $6)",
        {job, specification_json(), spec_time_ms, spec_memory_mb, reference_artefact, oracle});
  }
  return job;
}

auto transaction() -> std::unique_ptr<cms::db::Transaction> {
  return std::make_unique<cms::db::Transaction>(*cms::generate::test::live_client());
}

auto job_of(cms::db::connection& database, std::string_view task_id) -> std::int64_t {
  auto job = std::int64_t{0};
  for (const auto& row : database(sqlpp::select(cms::db::generationJobs.id)
                                      .from(cms::db::generationJobs)
                                      .where(cms::db::generationJobs.taskId == std::string{task_id})
                                      .order_by(cms::db::generationJobs.id.desc())
                                      .limit(1))) {
    job = row.id;
  }
  return job;
}

auto verification_of(cms::db::connection& database, std::string_view task_id) -> std::string {
  auto value = std::string{};
  for (const auto& row : database(sqlpp::select(cms::db::tasks.verification)
                                      .from(cms::db::tasks)
                                      .where(cms::db::tasks.id == std::string{task_id}))) {
    value = std::string{row.verification};
  }
  return value;
}

SQLPP_CREATE_NAME_TAG(certificate_rows);

using certificate_row = ::sqlpp::result_row_t<::sqlpp::field_spec_t<certificate_rows_t::_sqlpp_name_tag,
                                                                    ::sqlpp::integral>>;

auto certificate_count(cms::db::connection& database, std::string_view task_id) -> std::int64_t {
  auto rows = ::sqlpp::result_t<cms::db::result, certificate_row>{database.query_text(
      "SELECT count(*) AS certificate_rows FROM generation_certificates WHERE task_id = $1",
      {std::string{task_id}})};
  return rows.empty() ? 0 : rows.front().certificate_rows;
}

}  // namespace

TEST_CASE("a complete job is certified and the certificate says what it checked", "[certify][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto store = cms::generate::test::MemoryStore{};
  auto database = cms::generate::test::live_database();
  {
    auto fixture = transaction();
    (void)build_job(fixture->connection(), store, "certify-ok", reference_source(), true, 1000, 256);
    fixture->commit();
  }
  auto holder = transaction();
  auto& connection = holder->connection();

  const auto certificate = cms::generate::certify_job(connection, store, job_of(connection, "certify-ok"), COMMIT);

  REQUIRE(certificate.has_value());
  CHECK(certificate->outcome == cms::generate::VerificationOutcome::verified);
  CHECK(certificate->commit_id == std::string{COMMIT});
  CHECK(!certificate->checks.empty());
  CHECK(certificate->digests.size() >= 6);
  holder->commit();
  CHECK(verification_of(database, "certify-ok") == "verified");
  CHECK(certificate_count(database, "certify-ok") == 1);
}

TEST_CASE("evidence that is missing refuses and writes nothing", "[certify][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto store = cms::generate::test::MemoryStore{};
  auto database = cms::generate::test::live_database();
  {
    auto fixture = transaction();
    (void)build_job(fixture->connection(), store, "certify-missing", reference_source(), false, 0, 0);
    fixture->commit();
  }
  auto holder = transaction();
  auto& connection = holder->connection();

  const auto certificate =
      cms::generate::certify_job(connection, store, job_of(connection, "certify-missing"), COMMIT);

  REQUIRE(!certificate.has_value());
  CHECK(certificate.error().message.find("no specification") != std::string::npos);
  holder->rollback();
  CHECK(verification_of(database, "certify-missing") == "pending");
  CHECK(certificate_count(database, "certify-missing") == 0);
}

TEST_CASE("stale limits refuse rather than certify", "[certify][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto store = cms::generate::test::MemoryStore{};
  auto database = cms::generate::test::live_database();
  {
    auto fixture = transaction();
    (void)build_job(fixture->connection(), store, "certify-stale", reference_source(), true, 500, 128);
    fixture->commit();
  }
  auto holder = transaction();
  auto& connection = holder->connection();

  const auto certificate =
      cms::generate::certify_job(connection, store, job_of(connection, "certify-stale"), COMMIT);

  REQUIRE(!certificate.has_value());
  CHECK(certificate.error().message.find("stale") != std::string::npos);
  holder->rollback();
  CHECK(verification_of(database, "certify-stale") == "pending");
  CHECK(certificate_count(database, "certify-stale") == 0);
}

TEST_CASE("an artefact that no longer matches its digest is refused by name", "[certify][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto store = cms::generate::test::MemoryStore{};
  auto database = cms::generate::test::live_database();
  {
    auto fixture = transaction();
    (void)build_job(fixture->connection(), store, "certify-digest", reference_source(), true, 1000, 256);
    fixture->commit();
  }
  auto holder = transaction();
  auto& connection = holder->connection();
  store.overwrite("certify/certify-digest/oracle.cpp", "int main() { return 7; }\n");

  const auto certificate =
      cms::generate::certify_job(connection, store, job_of(connection, "certify-digest"), COMMIT);

  REQUIRE(!certificate.has_value());
  CHECK(certificate.error().message.find("does not match the digest") != std::string::npos);
  holder->rollback();
  CHECK(verification_of(database, "certify-digest") == "pending");
  CHECK(certificate_count(database, "certify-digest") == 0);
}

TEST_CASE("a reference that disagrees with the oracle is failed, not verified", "[certify][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto store = cms::generate::test::MemoryStore{};
  auto database = cms::generate::test::live_database();
  {
    auto fixture = transaction();
    (void)build_job(fixture->connection(), store, "certify-failed", wrong_reference_source(), true, 1000, 256);
    fixture->commit();
  }
  auto holder = transaction();
  auto& connection = holder->connection();

  const auto certificate =
      cms::generate::certify_job(connection, store, job_of(connection, "certify-failed"), COMMIT);

  REQUIRE(certificate.has_value());
  CHECK(certificate->outcome == cms::generate::VerificationOutcome::failed);
  holder->commit();
  CHECK(verification_of(database, "certify-failed") == "failed");
  CHECK(certificate_count(database, "certify-failed") == 1);
}

TEST_CASE("the generator role cannot write a verification", "[certify][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto holder = transaction();
  auto& connection = holder->connection();
  auto refusal = std::string{};
  try {
    connection.execute_text("SET ROLE cmslearn_generator", {});
    connection.execute_text("UPDATE tasks SET verification = 'verified' WHERE status = 'draft'", {});
  } catch (const std::exception& failure) {
    refusal = failure.what();
  }
  holder->rollback();
  CHECK(refusal.find("permission denied for table tasks") != std::string::npos);
}
