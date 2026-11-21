// The definition tables carry the checks a compiler used to give for free (ADR 0024). Each case breaks one
// check against a real database and expects the database to refuse, because the alternative is a definition
// the form renders one way and the validator checks another.

#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <drogon/orm/DbClient.h>
#include <sqlpp23/sqlpp23.h>

#include <cms/db/connection.hpp>
#include <cms/db/tables.hpp>
#include <cms/db/transaction.hpp>

namespace {

constexpr std::string_view ACTOR = "a0000001-1111-4111-8111-0000000000ab";
constexpr std::string_view TYPE_ID = "probe_type";
constexpr std::string_view TASK_ID = "probe-field-task";

auto has_live_database() -> bool { return std::getenv("CMSLEAN_TEST_DSN") != nullptr; }

auto live_client() -> const std::shared_ptr<::drogon::orm::DbClient>& {
  const auto* dsn = std::getenv("CMSLEAN_TEST_DSN");
  REQUIRE(dsn != nullptr);
  static const auto client = ::drogon::orm::DbClient::newPgClient(std::string{dsn}, 4);
  return client;
}

auto live_database() -> cms::db::connection {
  return cms::db::connection{std::make_shared<const cms::db::connection_config>(
      cms::db::connection_config{.client = live_client()})};
}

/// Runs one statement as the fixture's actor, in a transaction of its own: the actor is transaction-local so
/// it is in force for the triggers, and a refusal aborts only this attempt rather than the case.
auto as_actor(std::string_view sql, std::vector<cms::db::bound_value> parameters) -> std::string {
  auto transaction = cms::db::Transaction{*live_client()};
  auto& connection = transaction.connection();
  connection.execute_text("SELECT set_config('cms.actor_id', $1, true)",
                          {std::string{ACTOR}});
  connection.execute_text("SELECT set_config('cms.actor_kind', 'admin', true)", {});
  connection.execute_text("SELECT set_config('cms.invoked_by', 'fields_test', true)", {});
  try {
    connection.execute_text(std::string{sql}, std::move(parameters));
  } catch (const std::exception& failure) {
    return std::string{failure.what()};
  }
  transaction.commit();
  return {};
}

auto refused_with(const std::string& message, std::string_view expected) -> bool {
  return message.find(expected) != std::string::npos;
}

/// One task type and one draft task of it, written the way a definition write has to be written.
auto prepare(cms::db::connection& database) -> void {
  (void)database;
  CHECK(as_actor("INSERT INTO admin_accounts (id, handle, status) VALUES ($1::uuid, 'fields_seeder', 'active') ON CONFLICT (id) DO NOTHING",
                 {std::string{ACTOR}}).empty());
  CHECK(as_actor("INSERT INTO task_types (id, title_en, position) VALUES ($1, 'Probe', 900) "
                 "ON CONFLICT (id) DO NOTHING",
                 {std::string{TYPE_ID}}).empty());
  CHECK(as_actor("INSERT INTO tasks (id, version, task_type, status, title_en, title_th, difficulty, "
                 "time_limit_ms, memory_used_limit_mb, memory_reserved_limit_mb, checker, origin, verification, solution, content_hash) "
                 "VALUES ($1, 1, $2, 'draft', 'Probe', 'ทดสอบ', 800, 1000, 256, 512, 'token', 'original', "
                 "'pending', 'int main() {}', repeat('a', 64)) ON CONFLICT DO NOTHING",
                 {std::string{TASK_ID}, std::string{TYPE_ID}}).empty());
}

}  // namespace

TEST_CASE("a definition that cannot be rendered is refused by the table", "[fields][live]") {
  if (!has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = live_database();
  prepare(database);
  (void)database;

  CHECK(refused_with(
      as_actor("INSERT INTO task_field_defs (task_type_id, key, kind, label_en, position) "
               "VALUES ($1, 'shape', 'choice', 'Shape', 1)",
               {std::string{TYPE_ID}}),
      "task_field_defs_choices"));

  CHECK(refused_with(
      as_actor("INSERT INTO task_field_defs (task_type_id, key, kind, label_en, position) "
               "VALUES ($1, 'count', 'integer', 'Count', 2)",
               {std::string{TYPE_ID}}),
      "task_field_defs_bounds"));

  CHECK(refused_with(
      as_actor("INSERT INTO task_field_defs (task_type_id, key, kind, label_en, members, position) "
               "VALUES ($1, 'parts', 'collection', 'Parts', '[\"missing\"]'::jsonb, 3)",
               {std::string{TYPE_ID}}),
      "do not exist"));
}

TEST_CASE("an empty required field and a definition in use are both refused",
          "[fields][live]") {
  if (!has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = live_database();
  prepare(database);
  (void)database;
  CHECK(as_actor("INSERT INTO task_field_defs (task_type_id, key, kind, label_en, required, position) "
                 "VALUES ($1, 'statement', 'text', 'Statement', true, 1) ON CONFLICT DO NOTHING",
                 {std::string{TYPE_ID}})
            .empty());

  CHECK(refused_with(
      as_actor("INSERT INTO task_field_values (task_id, task_version, field_key, value) "
               "VALUES ($1, 1, 'statement', '\"\"'::jsonb)",
               {std::string{TASK_ID}}),
      "required"));

  CHECK(as_actor("INSERT INTO task_field_values (task_id, task_version, field_key, value) "
                 "VALUES ($1, 1, 'statement', '\"the text\"'::jsonb)",
                 {std::string{TASK_ID}})
            .empty());

  CHECK(refused_with(
      as_actor("DELETE FROM task_field_defs WHERE task_type_id = $1 AND key = 'statement'",
               {std::string{TYPE_ID}}),
      "holds"));
}

TEST_CASE("a definition change writes one audit row naming the actor", "[fields][live]") {
  if (!has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = live_database();
  prepare(database);
  CHECK(as_actor("INSERT INTO task_field_defs (task_type_id, key, kind, label_en, position) "
                 "VALUES ($1, 'audited', 'text', 'Audited', 9) ON CONFLICT DO NOTHING",
                 {std::string{TYPE_ID}})
            .empty());

  auto rows = std::int64_t{0};
  for (const auto& row : database(sqlpp::select(sqlpp::count(sqlpp::value(1)).as(sqlpp::alias::a))
                                      .from(cms::db::auditEvents)
                                      .where(cms::db::auditEvents.subject ==
                                                 std::string{TYPE_ID} + std::string{".audited"} &&
                                             cms::db::auditEvents.action ==
                                                 std::string{"task.field.insert"} &&
                                             cms::db::auditEvents.adminActorId ==
                                                 std::string{ACTOR}))) {
    rows = row.a;
  }
  CHECK(rows == 1);
}
