// A subtask is a row worth points, defined by a constraint, and a task's subtasks are a partition of its total
// (ADR 0033). These cases drive the rules from the outside: the database refuses a set that breaks them, and the
// statement's scoring section is rendered from the rows the judge reads, so the two cannot drift.

#include <cms/bank/bank.hpp>
#include <cms/bank/scoring.hpp>
#include <cms/bank/seed.hpp>
#include <cms/db/connection.hpp>
#include <cms/db/tables.hpp>
#include <cms/db/transaction.hpp>
#include <cms/testing/live_database.hpp>
#include <cms/testing/scratch_directory.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <string_view>
#include <system_error>

namespace {

constexpr std::string_view ACTOR = "a0000001-1111-4111-8111-0000000000b1";
constexpr std::string_view TYPE_ID = "subtask_probe";
constexpr std::string_view SUBTASK_COLUMNS =
    "task_id, task_version, position, points, constraint_text";

using cms::bank::Subtask;
using cms::bank::Task;

/// Runs @p work in a transaction with the audit actor set for its length: the settings tables refuse a definition
/// write that names no actor, and a pooled connection may put the setting and the write on different sessions.
auto in_transaction(const std::function<void(cms::db::connection&)>& work) -> void {
  auto transaction = cms::db::Transaction{*cms::test::live_client()};
  auto& database = transaction.connection();
  database.execute_text("SELECT set_config('cms.actor_id', $1, true)", {std::string{ACTOR}});
  database.execute_text("SELECT set_config('cms.actor_kind', 'admin', true)", {});
  database.execute_text("SELECT set_config('cms.invoked_by', 'subtasks_test', true)", {});
  work(database);
  transaction.commit();
}

/// One task type whose score mode is a choice, so a fixture can select `subtasks`.
auto make_type(cms::db::connection& database) -> void {
  database.execute_text(
      "INSERT INTO admin_accounts (id, handle, status) VALUES ($1::uuid, 'subtasks_probe', 'active') "
      "ON CONFLICT (id) DO NOTHING",
      {std::string{ACTOR}});
  database.execute_text(
      "INSERT INTO task_types (id, title_en, position) VALUES ($1, 'Probe', 902) "
      "ON CONFLICT (id) DO NOTHING",
      {std::string{TYPE_ID}});
  database.execute_text(
      "INSERT INTO task_field_defs (task_type_id, key, kind, label_en, choices, position) "
      "VALUES ($1, 'score_mode', 'choice', 'Score mode', '[\"per_case\",\"all_or_nothing\",\"subtasks\"]', 1) "
      "ON CONFLICT DO NOTHING",
      {std::string{TYPE_ID}});
}

/// A draft task of the probe type, with the fields a row needs and the score mode it scores by.
auto prepare_task(cms::db::connection& database, std::string_view task_id, std::string_view score_mode,
                  std::string_view content_hash) -> void {
  make_type(database);
  database.execute_text(
      "INSERT INTO tasks (id, version, task_type, status, title_en, title_th, difficulty, time_limit_ms, "
      "memory_limit_mb, points, checker, origin, verification, solution, content_hash) "
      "VALUES ($1, 1, $2, 'draft', 'Probe', 'ทดสอบ', 900, 1000, 256, 100, 'token', 'original', 'pending', '', $3)",
      {std::string{task_id}, std::string{TYPE_ID}, std::string{content_hash}});
  database.execute_text(
      "INSERT INTO task_field_values (task_id, task_version, field_key, value) "
      "VALUES ($1, 1, 'score_mode', to_jsonb($2::text))",
      {std::string{task_id}, std::string{score_mode}});
}

auto task_with(std::string_view id, std::string_view hash, std::int32_t points,
               std::vector<Subtask> subtasks) -> Task {
  auto task = Task{};
  task.id = std::string{id};
  task.version = 1;
  task.status = "draft";
  task.task_type = std::string{TYPE_ID};
  task.score_mode = "subtasks";
  task.title_en = "Probe";
  task.title_th = "ทดสอบ";
  task.difficulty = 900;
  task.time_limit_ms = 1000;
  task.memory_limit_mb = 256;
  task.points = points;
  task.subtasks = std::move(subtasks);
  task.checker_kind = "token";
  task.origin_kind = "original";
  task.verification_status = "pending";
  task.statements = {{"en", "# Probe\n\nBody."}};
  task.content_hash = std::string{hash};
  return task;
}

auto subtask_type() -> cms::bank::TaskTypeDefinition {
  auto definition = cms::bank::TaskTypeDefinition{};
  definition.id = std::string{TYPE_ID};
  definition.title_en = "Probe";
  definition.position = 902;
  auto field = cms::bank::FieldDefinition{};
  field.key = "score_mode";
  field.kind = "choice";
  field.label_en = "Score mode";
  field.choices = {"per_case", "all_or_nothing", "subtasks"};
  field.position = 1;
  definition.fields.push_back(field);
  return definition;
}

auto seed_bank(const Task& task, std::string_view bank_hash) -> cms::Result<cms::bank::SeedReport> {
  auto transaction = cms::db::Transaction{*cms::test::live_client()};
  auto bank = cms::bank::Bank{};
  bank.task_types.push_back(subtask_type());
  bank.tasks.push_back(task);
  bank.content_hash = std::string{bank_hash};
  auto report = cms::bank::seed(transaction.connection(), bank, nullptr);
  if (!report) {
    return report;
  }
  transaction.commit();
  return report;
}

/// @return The refusal the database gives for @p statement, empty when it ran. The decision checks are deferred, so
/// pending constraints are forced before the answer is read.
auto refusal(std::string_view statement, std::vector<cms::db::bound_value> parameters) -> std::string {
  auto transaction = cms::db::Transaction{*cms::test::live_client()};
  auto& database = transaction.connection();
  try {
    database.execute_text(std::string{statement}, std::move(parameters));
    database.execute_text("SET CONSTRAINTS ALL IMMEDIATE", {});
  } catch (const std::exception& failure) {
    return std::string{failure.what()};
  }
  return {};
}

auto statement_body(cms::db::connection& database, std::string_view task_id) -> std::string {
  auto body = std::string{};
  for (const auto& row : database(sqlpp::select(cms::db::taskStatements.body)
                                      .from(cms::db::taskStatements)
                                      .where(cms::db::taskStatements.taskId == std::string{task_id} &&
                                             cms::db::taskStatements.locale == std::string{"en"}))) {
    body = std::string{row.body};
  }
  return body;
}

SQLPP_CREATE_NAME_TAG(subtask_count);

auto subtask_rows(cms::db::connection& database, std::string_view task_id) -> std::int64_t {
  const auto one = sqlpp::value(1);
  auto count = std::int64_t{0};
  for (const auto& row : database(sqlpp::select(sqlpp::count(one).as(subtask_count))
                                      .from(cms::db::taskSubtasks)
                                      .where(cms::db::taskSubtasks.taskId == std::string{task_id}))) {
    count = row.subtask_count;
  }
  return count;
}

auto write_text(const std::filesystem::path& path, std::string_view text) -> void {
  std::filesystem::create_directories(path.parent_path());
  auto stream = std::ofstream{path, std::ios::trunc};
  stream << text;
  REQUIRE(stream.good());
}

}  // namespace

TEST_CASE("a subtask section is rendered from the rows, and the rows are a partition", "[subtasks]") {
  const auto section = cms::bank::scoring_section(
      "en", {Subtask{.position = 1, .points = 30, .constraint_text = "n <= 2000"},
             Subtask{.position = 2, .points = 70, .constraint_text = "n <= 200000"}},
      100);
  CHECK(section.find("## Scoring") != std::string::npos);
  CHECK(section.find("worth 100 points in 2 subtasks") != std::string::npos);
  CHECK(section.find("| 1 | n <= 2000 | 30 |") != std::string::npos);
  CHECK(section.find("| 2 | n <= 200000 | 70 |") != std::string::npos);

  const auto thai = cms::bank::scoring_section(
      "th", {Subtask{.position = 1, .points = 100, .constraint_text = "n <= 10"}}, 100);
  CHECK(thai.find("\u0e01\u0e32\u0e23\u0e43\u0e2b\u0e49\u0e04\u0e30\u0e41\u0e19\u0e19") != std::string::npos);

  CHECK(cms::bank::scoring_section("en", {}, 100).empty());
}

TEST_CASE("a task's task.json can name its points and its subtasks", "[subtasks]") {
  const auto root = cms::testing::scratch_directory("cms-subtasks-manifest");
  const auto task_dir = root / "content" / "tasks" / "probe-subtask";
  write_text(task_dir / "statement.en.md", "# Probe\n\nBody.");
  write_text(task_dir / "examples" / "1.in", "42\n");
  write_text(task_dir / "examples" / "1.out", "42\n");
  write_text(task_dir / "solutions" / "main.cpp", "int main() { return 0; }\n");
  write_text(task_dir / "task.json", R"({
    "id": "probe-subtask", "version": 1, "status": "draft",
    "title": {"en": "Probe", "th": "\u0e17\u0e14\u0e2a\u0e2d\u0e1a"},
    "topics": [], "difficulty": 900,
    "limits": {"time_ms": 1000, "memory_mb": 256},
    "points": 100,
    "subtasks": [
      {"position": 1, "points": 30, "constraint": "n <= 2000"},
      {"position": 2, "points": 70, "constraint": "n <= 200000"}
    ],
    "checker": {"kind": "token"}, "origin": {"kind": "original"},
    "verification": {"status": "pending"}
  })");

  const auto task = cms::bank::load_task(task_dir);
  REQUIRE(task.has_value());
  CHECK(task->points == 100);
  REQUIRE(task->subtasks.size() == 2);
  CHECK(task->subtasks[1].points == 70);
  CHECK(task->subtasks[1].constraint_text == "n <= 200000");

  write_text(task_dir / "task.json", R"({
    "id": "probe-subtask", "version": 1, "status": "draft",
    "title": {"en": "Probe", "th": "\u0e17\u0e14\u0e2a\u0e2d\u0e1a"},
    "topics": [], "difficulty": 900,
    "limits": {"time_ms": 1000, "memory_mb": 256},
    "subtasks": [
      {"position": 1, "points": 30, "constraint": "n <= 2000"},
      {"position": 1, "points": 70, "constraint": "n <= 200000"}
    ],
    "checker": {"kind": "token"}, "origin": {"kind": "original"},
    "verification": {"status": "pending"}
  })");

  const auto refused = cms::bank::load_task(task_dir);
  REQUIRE(!refused.has_value());
  CHECK(refused.error().message.find("named twice") != std::string::npos);
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
}

TEST_CASE("the seeder records a partition and the statement carries the section", "[subtasks][live]") {
  if (!cms::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::test::live_database();
  const auto task = task_with("probe-seeded", std::string(64, 'd'), 100,
                              {Subtask{.position = 1, .points = 30, .constraint_text = "n <= 2000"},
                               Subtask{.position = 2, .points = 70, .constraint_text = "n <= 200000"}});

  const auto report = seed_bank(task, std::string(64, 'e'));

  REQUIRE(report.has_value());
  CHECK(subtask_rows(database, "probe-seeded") == 2);
  const auto body = statement_body(database, "probe-seeded");
  CHECK(body.find("## Scoring") != std::string::npos);
  CHECK(body.find("| 1 | n <= 2000 | 30 |") != std::string::npos);
  CHECK(body.find("| 2 | n <= 200000 | 70 |") != std::string::npos);

  const auto refusal_text = refusal(
      "INSERT INTO task_subtasks (task_id, task_version, position, points, constraint_text) "
      "VALUES ($1, 1, 3, 60, 'every n')",
      {std::string{"probe-seeded"}});
  CHECK(refusal_text.find("add up to 100") != std::string::npos);

  const auto repeated = refusal(
      "INSERT INTO task_subtasks (task_id, task_version, position, points, constraint_text) "
      "VALUES ($1, 1, 1, 10, 'repeated')",
      {std::string{"probe-seeded"}});
  CHECK(repeated.find("task_subtasks_pkey") != std::string::npos);
}

TEST_CASE("a task that does not score by subtask cannot carry them", "[subtasks][live]") {
  if (!cms::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::test::live_database();
  in_transaction([&](cms::db::connection& connection) {
    prepare_task(connection, "probe-per-case", "per_case", std::string(64, 'f'));
  });

  const auto refused = refusal(
      "INSERT INTO task_subtasks (task_id, task_version, position, points, constraint_text) "
      "VALUES ($1, 1, 1, 100, 'every n')",
      {std::string{"probe-per-case"}});

  CHECK(refused.find("scores by per_case") != std::string::npos);
  CHECK(subtask_rows(database, "probe-per-case") == 0);
}

TEST_CASE("a subtask-scored task cannot be published with no subtasks", "[subtasks][live]") {
  if (!cms::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::test::live_database();
  in_transaction([&](cms::db::connection& connection) {
    prepare_task(connection, "probe-unpublished", "subtasks", std::string(64, 'a'));
  });

  const auto refused = refusal("UPDATE tasks SET status = 'published' WHERE id = $1",
                               {std::string{"probe-unpublished"}});
  CHECK(refused.find("cannot be published") != std::string::npos);
}

TEST_CASE("a subtask row carries no limits of its own", "[subtasks][live]") {
  if (!cms::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  // A limit on a subtask would be a second home for a fact the task states once (ADR 0033 §2): the column must not
  // exist, so a writer that tries to add one is refused for the schema's shape rather than by a rule nobody reads.
  const auto with_limit = refusal(
      "INSERT INTO task_subtasks (task_id, task_version, position, points, constraint_text, time_limit_ms) "
      "VALUES ($1, 1, 9, 10, 'x', 1000)",
      {std::string{"probe-peered"}});
  CHECK(with_limit.find("time_limit_ms") != std::string::npos);
  CHECK(with_limit.find("column") != std::string::npos);
}

TEST_CASE("moving a task's total while its subtasks exist is refused", "[subtasks][live]") {
  if (!cms::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::test::live_database();
  in_transaction([&](cms::db::connection& connection) {
    prepare_task(connection, "probe-total", "subtasks", std::string(64, 'b'));
    connection.execute_text(
        "INSERT INTO task_subtasks (task_id, task_version, position, points, constraint_text) "
        "VALUES ($1, 1, 1, 30, 'n <= 2000'), ($1, 1, 2, 70, 'n <= 200000')",
        {std::string{"probe-total"}});
  });

  const auto refused = refusal("UPDATE tasks SET points = 90 WHERE id = $1",
                               {std::string{"probe-total"}});

  CHECK(refused.find("add up to") != std::string::npos);
}
