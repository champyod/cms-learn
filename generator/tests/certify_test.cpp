// Step 7 is the only path to a verified task: it re-reads every artefact steps 3, 5 and 6 cited, re-runs the three
// methods ADR 0046 §4 requires (the reference pass, the brute-force differential and the half-limit margin of 0048),
// and writes what it checked, the commit it checked and the digests it saw. Incomplete or stale evidence is refused
// and nothing is written.

#include <cms/generate/certify.hpp>
#include <cms/generate/specification.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <vector>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sqlpp23/sqlpp23.h>

#include <cms/db/connection.hpp>
#include <cms/db/tables.hpp>
#include <cms/db/transaction.hpp>
#include <cms/object_store.hpp>

#include "live_database.hpp"
#include "memory_store.hpp"

namespace {

using cms::generate::Specification;
using cms::generate::SubtaskConstraint;

constexpr std::string_view COMMIT = "a1b2c3d4e5f60718293a4b5c6d7e8f9012345678";
constexpr std::string_view TYPE_ID = "certify_type";
constexpr std::int64_t TASK_TIME_MS = 1000;
constexpr std::int64_t TASK_MEMORY_MB = 256;

/// The brute force: read n and n values, print their sum.
constexpr std::string_view ORACLE_SOURCE = R"(#include <cstdio>
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

/// The intended solution: the same answer, so every method accepts it.
constexpr std::string_view REFERENCE_SOURCE = R"(#include <cstdio>
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

/// A reference that is right only at n = 1: the differential finds it on the case at the subtask's bound.
constexpr std::string_view SMALLEST_ONLY_REFERENCE = R"(#include <cstdio>
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

/// A correct reference that spends 800 ms busy-waiting on the task's largest case, so that case leaves less than
/// the half-limit margin of its 1000 ms declared limit.
constexpr std::string_view SLOW_REFERENCE = R"(#include <chrono>
#include <cstdio>
int main() {
  int count = 0;
  if (std::scanf("%d", &count) != 1) return 1;
  long long total = 0;
  for (int index = 0; index < count; ++index) {
    long long value = 0;
    if (std::scanf("%lld", &value) != 1) return 1;
    total = total + value;
  }
  if (count >= 20) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(800);
    while (std::chrono::steady_clock::now() < until) {
    }
  }
  std::printf("%lld\n", total);
  return 0;
}
)";

/// A correct reference that touches 200 MB, above the 128 MB half of the task's 256 MB limit, so 0048's memory margin
/// has something real to refuse.
constexpr std::string_view BLOATED_REFERENCE = R"(#include <cstdio>
#include <vector>
int main() {
  int count = 0;
  if (std::scanf("%d", &count) != 1) return 1;
  long long total = 0;
  for (int index = 0; index < count; ++index) {
    long long value = 0;
    if (std::scanf("%lld", &value) != 1) return 1;
    total = total + value;
  }
  auto block = std::vector<long long>(25 * 1024 * 1024, 1);
  long long touched = 0;
  for (const auto value : block) {
    touched += value;
  }
  std::printf("%lld\n", total + (touched == 0 ? 1 : 0));
  return 0;
}
)";

/// A correct reference that stays small, so its own footprint can be measured from a bloated process.
constexpr std::string_view SMALL_REFERENCE = R"(#include <cstdio>
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

/// A generator implementing the contract: stdin "<seed> <n>", stdout "n" then n values in the declared bounds.
constexpr std::string_view GENERATOR_SOURCE = R"(#include <cstdio>
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
constexpr std::string_view CHECKER_SOURCE = R"(#include <fstream>
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

/// The most dangerous artefact in the pipeline: a checker that says yes to everything.
constexpr std::string_view ALWAYS_OK_CHECKER = R"(int main() { return 0; }
)";

/// The partial for subtask 1: right up to the subtask's bound, wrong above it.
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
  std::printf("%lld\n", count <= 10 ? total : 42LL);
  return 0;
}
)";

/// The partial for subtask 2: it earns that group on its own cases, loses subtask 1's case, and is wrong above the
/// task's full bound.
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
  std::printf("%lld\n", count == 10 || count > 20 ? 42LL : total);
  return 0;
}
)";

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
                     std::string_view kind, std::string_view key, std::string_view digest,
                     std::int64_t call_index = 0) -> std::int64_t {
  auto id = std::int64_t{0};
  database.execute_text(
      "INSERT INTO generation_artefacts (job_id, step, kind, object_key, digest, model_id, prompt_version, seed, "
      "call_index) VALUES ($1, $2, $3, $4, $5, 'm', 'v', 1, $6)",
      {job, static_cast<std::int64_t>(step), std::string{kind}, std::string{key}, std::string{digest}, call_index});
  for (const auto& row : database(sqlpp::select(cms::db::generationArtefacts.id)
                                      .from(cms::db::generationArtefacts)
                                      .order_by(cms::db::generationArtefacts.id.desc())
                                      .limit(1))) {
    id = row.id;
  }
  return id;
}

/// The specification row's shape, as specification_json writes it: two subtasks so a partial can be earned inside
/// one group and refused outside it.
auto specification_json() -> std::string {
  return R"({"input":"n then n values","output":"their sum","input_shape":"n_then_values",)"
         R"("bounds":{"min_n":1,"max_n":30,"value_min":0,"value_max":9},)"
         R"("subtasks":[{"position":1,"max_n":10},{"position":2,"max_n":20}]})";
}

/// What a case substitutes into the complete fixture: the reference's source, the checker's source, and the limits
/// step 3 derived (which must equal the task's own unless the case is about stale evidence).
struct JobSources {
  std::string_view reference{REFERENCE_SOURCE};
  std::string_view checker{CHECKER_SOURCE};
  std::int64_t spec_time_ms{TASK_TIME_MS};
  std::int64_t spec_memory_mb{TASK_MEMORY_MB};
};

/// One job whose evidence is complete: a task with two subtasks, the specification row, the oracle, the reference,
/// one solution per subtask, both programs and one case set per subtask — every artefact written to a store a case
/// can fault later.
auto build_job(cms::db::connection& database, cms::generate::test::MemoryStore& store, std::string_view task_id,
               const JobSources& sources, bool with_specification) -> std::int64_t {
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
      "VALUES ($1, 1, $2, 'draft', 'Probe', 'ทดสอบ', 900, $3, $4, 100, 'token', 'original', 'pending', '', $5)",
      {std::string{task_id}, std::string{TYPE_ID}, TASK_TIME_MS, TASK_MEMORY_MB, std::string(64, 'c')});
  database.execute_text(
      "INSERT INTO task_field_values (task_id, task_version, field_key, value) "
      "VALUES ($1, 1, 'score_mode', to_jsonb('subtasks'::text))",
      {std::string{task_id}});
  database.execute_text(
      "INSERT INTO task_subtasks (task_id, task_version, position, points, min_n, max_n) "
      "VALUES ($1, 1, 1, 40, 1, 10), ($1, 1, 2, 60, 1, 20)",
      {std::string{task_id}});
  // The job starts queued and walks to awaiting-evidence through the state machine's own moves: a row cannot be created
  // already advanced, because its state is its history (ADR 0046 §2, ADR 0050).
  database.execute_text("INSERT INTO generation_jobs (task_id, task_version) VALUES ($1, 1)", {std::string{task_id}});
  auto job = std::int64_t{0};
  for (const auto& row : database(sqlpp::select(cms::db::generationJobs.id)
                                      .from(cms::db::generationJobs)
                                      .where(cms::db::generationJobs.taskId == std::string{task_id})
                                      .order_by(cms::db::generationJobs.id.desc())
                                      .limit(1))) {
    job = row.id;
  }
  // The two legal moves the worker makes before the certifier runs, so the fixture ends where the record lets a job end
  // before a certificate decides it.
  database.execute_text("UPDATE generation_jobs SET state = 'running', claimed_by = 'certify-fixture', "
                        "claimed_at = now(), lease_expires_at = now() + interval '5 minutes' WHERE id = $1", {job});
  database.execute_text("UPDATE generation_jobs SET state = 'awaiting-evidence', claimed_by = NULL, claimed_at = NULL, "
                        "lease_expires_at = NULL WHERE id = $1", {job});

  const auto write = [&](std::string_view key, std::string_view bytes) {
    store.overwrite(key, bytes);
    return cms::store::digest_of(bytes);
  };
  const auto key_for = [task_id](std::string_view name) {
    return std::string{"certify/"} + std::string{task_id} + "/" + std::string{name};
  };

  const auto oracle_key = key_for("oracle.cpp");
  const auto reference_key = key_for("reference.cpp");
  const auto first_key = key_for("subtask-1.cpp");
  const auto second_key = key_for("subtask-2.cpp");
  const auto generator_key = key_for("generator.cpp");
  const auto checker_key = key_for("checker.cpp");
  const auto cases_key = key_for("cases.json");

  const auto oracle = insert_artefact(database, job, 3, "oracle", oracle_key, write(oracle_key, ORACLE_SOURCE));
  const auto reference_artefact =
      insert_artefact(database, job, 5, "reference", reference_key, write(reference_key, sources.reference));
  const auto first = insert_artefact(database, job, 5, "subtask-solution", first_key, write(first_key, FIRST_PARTIAL),
                                     1);
  const auto second =
      insert_artefact(database, job, 5, "subtask-solution", second_key, write(second_key, SECOND_PARTIAL), 2);
  const auto generator =
      insert_artefact(database, job, 6, "generator", generator_key, write(generator_key, GENERATOR_SOURCE));
  const auto checker = insert_artefact(database, job, 6, "checker", checker_key, write(checker_key, sources.checker));
  const auto cases = insert_artefact(
      database, job, 6, "cases", cases_key, write(cases_key, R"([{"seed":1,"n":1},{"seed":2,"n":10}])"));

  database.execute_text(
      "INSERT INTO generation_solutions (job_id, position, artefact_id, proof) VALUES "
      "($1, 0, $2, 'the reference was checked'), ($1, 1, $3, 'the partial was checked'), "
      "($1, 2, $4, 'the partial was checked')",
      {job, reference_artefact, first, second});
  database.execute_text(
      "INSERT INTO generation_programs (job_id, role, artefact_id, proof) VALUES "
      "($1, 'generator', $2, 'deterministic'), ($1, 'checker', $3, 'planted wrong answer refused')",
      {job, generator, checker});
  database.execute_text(
      "INSERT INTO generation_cases (job_id, subtask_position, artefact_id, case_count, proof) "
      "VALUES ($1, 1, $2, 2, 'two cases'), ($1, 2, $2, 2, 'two cases')",
      {job, cases});

  if (with_specification) {
    database.execute_text(
        "INSERT INTO generation_specifications (job_id, specification, time_limit_ms, memory_limit_mb, derivation, "
        "answer_artefact_id, oracle_artefact_id) VALUES ($1, $2::jsonb, $3, $4, 'derived', $5, $6)",
        {job, specification_json(), sources.spec_time_ms, sources.spec_memory_mb, reference_artefact, oracle});
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

/// @return The artefact step 5 recorded for @p position in @p job.
auto solution_artefact(cms::db::connection& database, std::int64_t job, std::int32_t position) -> std::int64_t {
  auto id = std::int64_t{0};
  for (const auto& row : database(sqlpp::select(cms::db::generationSolutions.artefactId)
                                      .from(cms::db::generationSolutions)
                                      .where(cms::db::generationSolutions.jobId == job &&
                                             cms::db::generationSolutions.position == position))) {
    id = row.artefactId;
  }
  return id;
}

/// @return The outcome the newest certificate for @p task_id records.
auto newest_outcome(cms::db::connection& database, std::string_view task_id) -> std::string {
  auto value = std::string{};
  for (const auto& row : database(sqlpp::select(cms::db::generationCertificates.outcome)
                                      .from(cms::db::generationCertificates)
                                      .where(cms::db::generationCertificates.taskId == std::string{task_id})
                                      .order_by(cms::db::generationCertificates.createdAt.desc())
                                      .limit(1))) {
    value = std::string{row.outcome};
  }
  return value;
}

/// @return The checks the newest certificate for @p task_id records, as one text.
auto newest_checks(cms::db::connection& database, std::string_view task_id) -> std::string {
  auto value = std::string{};
  for (const auto& row : database(sqlpp::select(cms::db::generationCertificates.checks)
                                      .from(cms::db::generationCertificates)
                                      .where(cms::db::generationCertificates.taskId == std::string{task_id})
                                      .order_by(cms::db::generationCertificates.createdAt.desc())
                                      .limit(1))) {
    value = std::string{row.checks};
  }
  return value;
}

/// @return Whether one check the certificate recorded mentions @p needle.
auto mentions(const std::vector<std::string>& checks, std::string_view needle) -> bool {
  return std::any_of(checks.begin(), checks.end(), [needle](const std::string& check) {
    return check.find(needle) != std::string::npos;
  });
}

}  // namespace

TEST_CASE("the specification row is written and read by one definition", "[certify]") {
  const auto written = cms::generate::specification_json(cms::generate::Specification{
      .input = "n then n values",
      .output = "their sum",
      .bounds = {.min_n = 1, .max_n = 30, .value_min = 0, .value_max = 9},
      .subtasks = {SubtaskConstraint{.position = 1, .bounds = cms::SubtaskBounds{.max_n = 10}},
                   SubtaskConstraint{.position = 2, .bounds = cms::SubtaskBounds{.max_n = 20}}}});

  const auto read = cms::generate::read_specification(written);

  REQUIRE(read.has_value());
  CHECK(read->input == "n then n values");
  CHECK(read->output == "their sum");
  CHECK(read->bounds.min_n == 1);
  CHECK(read->bounds.max_n == 30);
  CHECK(read->bounds.value_min == 0);
  CHECK(read->bounds.value_max == 9);
  REQUIRE(read->subtasks.size() == 2);
  CHECK(read->subtasks[0].position == 1);
  CHECK(read->subtasks[0].bounds.max_n == 10);
  CHECK(read->subtasks[1].position == 2);
  CHECK(read->subtasks[1].bounds.max_n == 20);
}

TEST_CASE("a stored specification that lost a fact is refused by name", "[certify]") {
  const auto read = cms::generate::read_specification(
      R"({"input":"n then n values","input_shape":"n_then_values","bounds":{"min_n":1,"max_n":30,"value_min":0,)"
      R"("value_max":9},"subtasks":[{"position":1,"max_n":10}]})");

  REQUIRE(!read.has_value());
  CHECK(read.error().message.find("missing") != std::string::npos);
  CHECK(read.error().message.find("output") != std::string::npos);
}

TEST_CASE("a stored subtask bound that is not an integer is refused by name", "[certify]") {
  const auto read = cms::generate::read_specification(
      R"({"input":"n then n values","output":"their sum","input_shape":"n_then_values",)"
      R"("bounds":{"min_n":1,"max_n":30,"value_min":0,"value_max":9},)"
      R"("subtasks":[{"position":1,"max_n":"10"}]})");

  REQUIRE(!read.has_value());
  CHECK(read.error().message.find("max_n") != std::string::npos);
  CHECK(read.error().message.find("not an integer") != std::string::npos);
}

TEST_CASE("a complete job is certified and the certificate names every method it re-ran", "[certify][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto store = cms::generate::test::MemoryStore{};
  auto database = cms::generate::test::live_database();
  {
    auto fixture = transaction();
    (void)build_job(fixture->connection(), store, "certify-ok", JobSources{}, true);
    fixture->commit();
  }
  auto holder = transaction();
  auto& connection = holder->connection();

  const auto certificate = cms::generate::certify_job(connection, store, job_of(connection, "certify-ok"), COMMIT);

  if (certificate.has_value() && certificate->outcome != cms::generate::VerificationOutcome::verified) {
    for (const auto& check : certificate->checks) {
      UNSCOPED_INFO("a method said: " + check);
    }
  }
  REQUIRE(certificate.has_value());
  CHECK(certificate->outcome == cms::generate::VerificationOutcome::verified);
  CHECK(certificate->commit_id == std::string{COMMIT});
  CHECK(certificate->artefacts.size() >= 8);
  CHECK(mentions(certificate->checks, "the test cases the generator prints"));
  CHECK(mentions(certificate->checks, "the reference and the subtask solutions through the checker"));
  CHECK(mentions(certificate->checks, "the brute-force differential"));
  CHECK(mentions(certificate->checks, "the reference inside half the declared limits"));
  CHECK(mentions(certificate->checks, "inside the 500 ms and 128 MB halves of the declared 1000 ms and 256 MB limits"));
  holder->commit();
  CHECK(verification_of(database, "certify-ok") == "verified");
  CHECK(certificate_count(database, "certify-ok") == 1);
}

TEST_CASE("a certificate whose artefact moved after the run no longer certifies", "[certify][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto store = cms::generate::test::MemoryStore{};
  auto database = cms::generate::test::live_database();
  {
    auto fixture = transaction();
    (void)build_job(fixture->connection(), store, "certify-moved", JobSources{}, true);
    fixture->commit();
  }
  auto job = std::int64_t{0};
  {
    auto holder = transaction();
    auto& connection = holder->connection();
    job = job_of(connection, "certify-moved");
    const auto certificate = cms::generate::certify_job(connection, store, job, COMMIT);
    REQUIRE(certificate.has_value());
    CHECK(certificate->outcome == cms::generate::VerificationOutcome::verified);
    holder->commit();
  }
  CHECK(verification_of(database, "certify-moved") == "verified");
  REQUIRE(certificate_count(database, "certify-moved") == 1);

  // The certificate still matches what it recorded, so it holds and nothing is written.
  {
    auto holder = transaction();
    const auto held = cms::generate::revalidate_certificate(holder->connection(), store, "certify-moved", 1);
    REQUIRE(held.has_value());
    CHECK(held->is_certified);
    CHECK(held->outcome == cms::generate::VerificationOutcome::verified);
    CHECK(held->changed.empty());
    holder->commit();
  }
  CHECK(certificate_count(database, "certify-moved") == 1);

  // An artefact is rewritten after the run: its row now holds a digest the certificate never saw.
  const auto reference = solution_artefact(database, job, 0);
  REQUIRE(reference != 0);
  const auto rewritten = std::string(64, 'e');
  database.execute_text("UPDATE generation_artefacts SET digest = $1 WHERE id = $2", {rewritten, reference});

  {
    auto holder = transaction();
    const auto moved = cms::generate::revalidate_certificate(holder->connection(), store, "certify-moved", 1);
    REQUIRE(moved.has_value());
    CHECK(moved->is_certified);
    CHECK(moved->outcome == cms::generate::VerificationOutcome::failed);
    REQUIRE(!moved->changed.empty());
    CHECK(moved->changed.front().find("the reference (step 5, position 0)") != std::string::npos);
    CHECK(moved->changed.front().find(rewritten) != std::string::npos);
    holder->commit();
  }

  // The stale row does not certify the task: the newest certificate is the failed one that names the change.
  CHECK(verification_of(database, "certify-moved") == "failed");
  CHECK(newest_outcome(database, "certify-moved") == "failed");
  CHECK(certificate_count(database, "certify-moved") == 2);
  CHECK(newest_checks(database, "certify-moved").find("the reference (step 5, position 0)") != std::string::npos);
}

TEST_CASE("a case count the generator's seeds do not reproduce is failed", "[certify][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto store = cms::generate::test::MemoryStore{};
  auto database = cms::generate::test::live_database();
  {
    auto fixture = transaction();
    const auto job = build_job(fixture->connection(), store, "certify-count", JobSources{}, true);
    fixture->connection().execute_text(
        "UPDATE generation_cases SET case_count = 3 WHERE job_id = $1 AND subtask_position = 1", {job});
    fixture->commit();
  }
  auto holder = transaction();
  auto& connection = holder->connection();

  const auto certificate =
      cms::generate::certify_job(connection, store, job_of(connection, "certify-count"), COMMIT);

  REQUIRE(certificate.has_value());
  CHECK(certificate->outcome == cms::generate::VerificationOutcome::failed);
  CHECK(mentions(certificate->checks, "step 6 recorded 3 case(s) for subtask 1"));
  CHECK(mentions(certificate->checks, "the generator's seeds produce 2"));
  holder->commit();
  CHECK(verification_of(database, "certify-count") == "failed");
}

TEST_CASE("evidence that is missing refuses and writes nothing", "[certify][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto store = cms::generate::test::MemoryStore{};
  auto database = cms::generate::test::live_database();
  {
    auto fixture = transaction();
    (void)build_job(fixture->connection(), store, "certify-missing", JobSources{}, false);
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
    (void)build_job(fixture->connection(), store, "certify-stale",
                    JobSources{.spec_time_ms = 500, .spec_memory_mb = 128}, true);
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
    (void)build_job(fixture->connection(), store, "certify-digest", JobSources{}, true);
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

TEST_CASE("a reference right only on the smallest case is failed, naming the case", "[certify][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto store = cms::generate::test::MemoryStore{};
  auto database = cms::generate::test::live_database();
  {
    auto fixture = transaction();
    (void)build_job(fixture->connection(), store, "certify-disagree",
                    JobSources{.reference = SMALLEST_ONLY_REFERENCE}, true);
    fixture->commit();
  }
  auto holder = transaction();
  auto& connection = holder->connection();

  const auto certificate =
      cms::generate::certify_job(connection, store, job_of(connection, "certify-disagree"), COMMIT);

  REQUIRE(certificate.has_value());
  CHECK(certificate->outcome == cms::generate::VerificationOutcome::failed);
  CHECK(mentions(certificate->checks, "the brute-force differential"));
  CHECK(mentions(certificate->checks, "n = 10"));
  CHECK(mentions(certificate->checks, "the reference answered \"42\""));
  holder->commit();
  CHECK(verification_of(database, "certify-disagree") == "failed");
  CHECK(certificate_count(database, "certify-disagree") == 1);
}

TEST_CASE("a checker that accepts a planted wrong answer is failed by name", "[certify][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto store = cms::generate::test::MemoryStore{};
  auto database = cms::generate::test::live_database();
  {
    auto fixture = transaction();
    (void)build_job(fixture->connection(), store, "certify-checker",
                    JobSources{.checker = ALWAYS_OK_CHECKER}, true);
    fixture->commit();
  }
  auto holder = transaction();
  auto& connection = holder->connection();

  const auto certificate =
      cms::generate::certify_job(connection, store, job_of(connection, "certify-checker"), COMMIT);

  REQUIRE(certificate.has_value());
  CHECK(certificate->outcome == cms::generate::VerificationOutcome::failed);
  CHECK(mentions(certificate->checks, "the reference and the subtask solutions through the checker"));
  CHECK(mentions(certificate->checks, "accepts a planted wrong answer"));
  CHECK(mentions(certificate->checks, "not a checker"));
  CHECK(mentions(certificate->checks, "the brute-force differential"));
  CHECK(mentions(certificate->checks, "the reference inside half the declared limits"));
  holder->commit();
  CHECK(verification_of(database, "certify-checker") == "failed");
}

TEST_CASE("the measured peak is the program's own, even when its parent holds a hundred megabytes", "[certify][live]") {
  // A parent that holds ~200 MB, the way a sanitizer-built test binary does: the program's own number must not move.
  auto bloat = std::vector<char>(200 * 1024 * 1024, 1);
  auto touched = std::int64_t{0};
  for (std::size_t index = 0; index < bloat.size(); index += 4096) {
    touched += bloat[index];
  }
  REQUIRE(touched > 0);

  const auto measured = cms::generate::measure_program(SMALL_REFERENCE, "3\n1 2 3\n", 1000);

  REQUIRE(measured.has_value());
  // Its own footprint: a few megabytes. The parent's 200 MB would arrive as ~204800 kB under the old reading.
  CHECK(measured->peak_memory_kb < 16 * 1024);
  CHECK(measured->peak_memory_kb > 0);
}

TEST_CASE("a reference above half the memory limit is failed, naming the value, the limit and the case",
          "[certify][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto store = cms::generate::test::MemoryStore{};
  auto database = cms::generate::test::live_database();
  {
    auto fixture = transaction();
    (void)build_job(fixture->connection(), store, "certify-bloated",
                    JobSources{.reference = BLOATED_REFERENCE}, true);
    fixture->commit();
  }
  auto holder = transaction();
  auto& connection = holder->connection();

  const auto certificate =
      cms::generate::certify_job(connection, store, job_of(connection, "certify-bloated"), COMMIT);

  REQUIRE(certificate.has_value());
  CHECK(certificate->outcome == cms::generate::VerificationOutcome::failed);
  CHECK(mentions(certificate->checks, "the reference inside half the declared limits"));
  CHECK(mentions(certificate->checks, "above the 128 MB half of the declared 256 MB memory limit"));
  CHECK(mentions(certificate->checks, "the reference measured "));
  CHECK(mentions(certificate->checks, "MB at n = "));
}

TEST_CASE("a reference above half the limit on its slowest case is failed", "[certify][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto store = cms::generate::test::MemoryStore{};
  auto database = cms::generate::test::live_database();
  {
    auto fixture = transaction();
    (void)build_job(fixture->connection(), store, "certify-slow",
                    JobSources{.reference = SLOW_REFERENCE}, true);
    fixture->commit();
  }
  auto holder = transaction();
  auto& connection = holder->connection();

  const auto certificate =
      cms::generate::certify_job(connection, store, job_of(connection, "certify-slow"), COMMIT);

  REQUIRE(certificate.has_value());
  CHECK(certificate->outcome == cms::generate::VerificationOutcome::failed);
  CHECK(mentions(certificate->checks, "the reference inside half the declared limits"));
  CHECK(mentions(certificate->checks, "above the 500 ms half of the declared 1000 ms time limit"));
  CHECK(mentions(certificate->checks, "at n = 20"));
  holder->commit();
  CHECK(verification_of(database, "certify-slow") == "failed");
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
