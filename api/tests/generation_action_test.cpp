#include "live_database.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>
#include <sqlpp23/sqlpp23.h>

#include <cms/api/admin_actions.hpp>
#include <cms/api/generation_page.hpp>
#include <cms/db/tables.hpp>

namespace {

SQLPP_CREATE_NAME_TAG(row_count);

constexpr std::string_view ENGINEER_ID = "a0000101-1111-4111-8111-000000000101";
constexpr std::string_view ENGINEER_HANDLE = "generation_engineer";
constexpr std::string_view ENGINEER_ROLE_ID = "b0000101-1111-4111-8111-000000000101";

constexpr std::string_view READER_ID = "a0000102-1111-4111-8111-000000000102";
constexpr std::string_view READER_HANDLE = "generation_reader";
constexpr std::string_view READER_ROLE_ID = "b0000102-1111-4111-8111-000000000102";

constexpr std::string_view TASK_ID = "generation-action-probe";
constexpr std::string_view BRIEF = "a task about adding n values, easy";

auto skip_without_database() -> void {
  if (std::getenv("CMSLEAN_TEST_DSN") == nullptr) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
}

auto acting(std::string_view id) -> cms::api::ActingAccount {
  return cms::api::ActingAccount{.kind = "admin", .id = std::string{id}};
}

auto count_rows(cms::db::connection& database, const auto& statement) -> std::int64_t {
  auto count = std::int64_t{0};
  for (const auto& row : database(statement)) {
    count = row.row_count;
  }
  return count;
}

/// The account, the role and the capability it holds: the same three rows an operator creates through the panel.
auto seed_capability(cms::db::connection& database, std::string_view admin_id, std::string_view handle,
                     std::string_view role_id, std::string_view role_name, std::string_view capability) -> void {
  const auto one = sqlpp::value(1);
  const auto seeded = count_rows(database, sqlpp::select(sqlpp::count(one).as(row_count))
                                               .from(cms::db::adminAccounts)
                                               .where(cms::db::adminAccounts.id == std::string{admin_id})) > 0;
  if (seeded) {
    return;
  }
  database(sqlpp::insert_into(cms::db::adminAccounts).set(
      cms::db::adminAccounts.id = std::string{admin_id},
      cms::db::adminAccounts.handle = std::string{handle},
      cms::db::adminAccounts.status = std::string{"active"}));
  database(sqlpp::insert_into(cms::db::roles).set(cms::db::roles.id = std::string{role_id},
                                                  cms::db::roles.name = std::string{role_name}));
  database(sqlpp::insert_into(cms::db::roleCapabilities).set(
      cms::db::roleCapabilities.roleId = std::string{role_id},
      cms::db::roleCapabilities.capability = std::string{capability}));
  database(sqlpp::insert_into(cms::db::adminRoles).set(
      cms::db::adminRoles.adminId = std::string{admin_id},
      cms::db::adminRoles.roleId = std::string{role_id}));
}

/// A job names a task version the table's foreign key can check, so the draft exists first.
auto seed_task(cms::db::connection& database) -> void {
  database.execute_text("DELETE FROM generation_jobs WHERE task_id = $1", {std::string{TASK_ID}});
  database.execute_text("DELETE FROM tasks WHERE id = $1", {std::string{TASK_ID}});
  database.execute_text(
      "INSERT INTO tasks (id, version, status, title_en, title_th, difficulty, time_limit_ms, "
      "memory_used_limit_mb, memory_reserved_limit_mb, checker, origin, verification, solution, content_hash) "
      "VALUES ($1, 1, 'draft', 'Probe', 'โพรบ', 900, 1000, 256, 512, 'token', 'original', 'pending', '', '')",
      {std::string{TASK_ID}});
}

auto prepare(cms::db::connection& database) -> void {
  seed_capability(database, ENGINEER_ID, ENGINEER_HANDLE, ENGINEER_ROLE_ID, "generation_engineer_role",
                  "generation:write");
  seed_capability(database, READER_ID, READER_HANDLE, READER_ROLE_ID, "generation_reader_role", "content:read");
  seed_task(database);
}

}  // namespace

TEST_CASE("the stream event carries the step line the page prints", "[generation]") {
  const auto job = cms::api::GenerationJobView{.id = "7",
                                               .task_id = std::string{TASK_ID},
                                               .state = "running",
                                               .refusal = {},
                                               .steps_line = "1 2 . - - - -",
                                               .steps_done = "2/7",
                                               .has_refusal = false};
  const auto payload = cms::api::generation_stream_payload(job);

  CHECK(payload.rfind("event: state\n", 0) == 0);
  CHECK(payload.find("\"steps_done\":\"2/7\"") != std::string::npos);
  CHECK(payload.find("\"steps_line\":\"1 2 . - - - -\"") != std::string::npos);
  CHECK(payload.find("\"has_refusal\":false") != std::string::npos);
  CHECK(payload.ends_with("\n\n"));
}

TEST_CASE("an account without generation:write is refused by the capability it lacks", "[generation][live]") {
  skip_without_database();
  auto database = cms::test::live_database();
  prepare(database);

  const auto refused = cms::api::create_generation_job(
      database, acting(READER_ID),
      {.task_id = std::string{TASK_ID}, .task_version = 1, .brief = std::string{BRIEF}});

  REQUIRE(!refused.has_value());
  CHECK(refused.error().code == cms::ErrorCode::permission_denied);
  CHECK(refused.error().message.find("generation:write") != std::string::npos);

  const auto one = sqlpp::value(1);
  CHECK(count_rows(database, sqlpp::select(sqlpp::count(one).as(row_count))
                                 .from(cms::db::generationJobs)
                                 .where(cms::db::generationJobs.taskId == std::string{TASK_ID})) == 0);
}

TEST_CASE("a brief is required, and an empty one writes no row", "[generation][live]") {
  skip_without_database();
  auto database = cms::test::live_database();
  prepare(database);

  const auto refused = cms::api::create_generation_job(
      database, acting(ENGINEER_ID),
      {.task_id = std::string{TASK_ID}, .task_version = 1, .brief = {}});

  REQUIRE(!refused.has_value());
  CHECK(refused.error().code == cms::ErrorCode::invalid_argument);
  CHECK(refused.error().message.find("brief") != std::string::npos);

  const auto one = sqlpp::value(1);
  CHECK(count_rows(database, sqlpp::select(sqlpp::count(one).as(row_count))
                                 .from(cms::db::generationJobs)
                                 .where(cms::db::generationJobs.taskId == std::string{TASK_ID})) == 0);
}

TEST_CASE("a job is created queued, and the screen reads it back with no step done", "[generation][live]") {
  skip_without_database();
  auto database = cms::test::live_database();
  prepare(database);

  const auto created = cms::api::create_generation_job(
      database, acting(ENGINEER_ID),
      {.task_id = std::string{TASK_ID}, .task_version = 1, .brief = std::string{BRIEF}});
  REQUIRE(created.has_value());

  const auto one = sqlpp::value(1);
  CHECK(count_rows(database, sqlpp::select(sqlpp::count(one).as(row_count))
                                 .from(cms::db::generationJobs)
                                 .where(cms::db::generationJobs.taskId == std::string{TASK_ID} &&
                                        cms::db::generationJobs.state == std::string{"queued"})) == 1);

  const auto jobs = cms::api::read_generation_jobs(database, 20);
  const auto found = std::find_if(jobs.begin(), jobs.end(), [](const auto& job) {
    return job.task_id == std::string{TASK_ID};
  });
  REQUIRE(found != jobs.end());
  CHECK(found->state == "queued");
  CHECK(found->steps_done == "0/7");
  CHECK(found->steps_line == "- - - - - - -");
  CHECK(!found->has_refusal);
}
