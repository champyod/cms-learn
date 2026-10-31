#include <chrono>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include <drogon/orm/DbClient.h>
#include <sqlpp23/sqlpp23.h>

#include <cms/db/connection.hpp>
#include <cms/db/settings_query.hpp>
#include <cms/db/tables.hpp>

namespace {

SQLPP_CREATE_NAME_TAG(kind_filter);
SQLPP_CREATE_NAME_TAG(status_filter);

auto two_text_parameters_query() {
  const auto kind = sqlpp::parameter(sqlpp::text{}, kind_filter);
  const auto status = sqlpp::parameter(sqlpp::text{}, status_filter);
  return sqlpp::select(cms::db::principals.id)
      .from(cms::db::principals)
      .where(cms::db::principals.kind == kind && cms::db::principals.status == status);
}

constexpr std::string_view SESSION_ID = "8f14e45f-ceea-467a-9c1e-0f5a2b3c4d5e";
constexpr std::string_view PRINCIPAL_ID = "c9f0f895-fb98-4c4b-8b0d-1a2b3c4d5e6f";
constexpr std::string_view SETTING_KEY = "live.probe";
constexpr std::string_view SETTING_VALUE = "round trip";

auto seeded_expiry() -> cms::db::timestamp {
  return std::chrono::sys_days{std::chrono::year{2100} / 1 / 2} + std::chrono::hours{3} +
         std::chrono::minutes{4} + std::chrono::seconds{5} +
         std::chrono::microseconds{123456};
}

auto seed(::drogon::orm::DbClient& client) -> void {
  client.execSqlSync("SELECT set_config('cms.actor_id', $1, false)", std::string{PRINCIPAL_ID});
  client.execSqlSync(
      "INSERT INTO principals (id, kind, handle, status) VALUES ($1, 'user', 'live_probe', "
      "'active') ON CONFLICT DO NOTHING",
      std::string{PRINCIPAL_ID});
  client.execSqlSync(
      "INSERT INTO sessions (id, principal_id, expires_at) VALUES ($1, $2, "
      "'2100-01-02 03:04:05.123456+00') ON CONFLICT (id) DO UPDATE SET expires_at = "
      "excluded.expires_at, revoked_at = NULL",
      std::string{SESSION_ID}, std::string{PRINCIPAL_ID});
  client.execSqlSync(
      "INSERT INTO settings (key, kind, value_text, tier) VALUES ($1, 'text', $2, 'public') "
      "ON CONFLICT (key) DO UPDATE SET value_text = excluded.value_text, tier = 'public'",
      std::string{SETTING_KEY}, std::string{SETTING_VALUE});
}

}  // namespace

TEST_CASE("a statement serializes to positional placeholders in the order it walks them",
          "[db][connection]") {
  const auto statement = cms::db::live_session_query();
  auto bindings = cms::db::bindings_for(statement);
  bindings.session_id = std::string{SESSION_ID};
  bindings.now = cms::db::timestamp{std::chrono::seconds{1'762'000'000}};

  auto target = cms::db::context<decltype(bindings)>{bindings};
  const auto sql = sqlpp::to_sql_string(target, statement);
  const auto parameters = target.bound_parameters();

  CHECK(sql ==
        "SELECT sessions.principal_id, sessions.expires_at, sessions.revoked_at FROM sessions "
        "WHERE (sessions.id = $1) AND (sessions.revoked_at IS NULL) AND (sessions.expires_at > $2)");
  REQUIRE(parameters.size() == 2);
  CHECK(std::get<std::string>(parameters[0]) == SESSION_ID);
  CHECK(std::get<cms::db::timestamp>(parameters[1]) ==
        cms::db::timestamp{std::chrono::seconds{1'762'000'000}});
}

TEST_CASE("two parameters of the same type keep their positions", "[db][connection]") {
  const auto statement = two_text_parameters_query();
  auto bindings = cms::db::bindings_for(statement);
  bindings.kind_filter = std::string{"user"};
  bindings.status_filter = std::string{"active"};

  auto target = cms::db::context<decltype(bindings)>{bindings};
  const auto sql = sqlpp::to_sql_string(target, statement);
  const auto parameters = target.bound_parameters();

  CHECK(sql ==
        "SELECT principals.id FROM principals WHERE (principals.kind = $1) AND "
        "(principals.status = $2)");
  REQUIRE(parameters.size() == 2);
  CHECK(std::get<std::string>(parameters[0]) == "user");
  CHECK(std::get<std::string>(parameters[1]) == "active");
}

TEST_CASE("a literal reaches the text escaped rather than raw", "[db][connection]") {
  const auto statement = sqlpp::select(cms::db::principals.id)
                             .from(cms::db::principals)
                             .where(cms::db::principals.handle ==
                                    sqlpp::value(std::string{"o'brien"}));

  auto bindings = cms::db::bindings_for(statement);
  auto target = cms::db::context<decltype(bindings)>{bindings};
  const auto sql = sqlpp::to_sql_string(target, statement);

  CHECK(sql == "SELECT principals.id FROM principals WHERE principals.handle = 'o''brien'");
  CHECK(target.bound_parameters().empty());
}

TEST_CASE("a seeded row round-trips through the connector", "[db][connection][live]") {
  const auto* dsn = std::getenv("CMSLEAN_TEST_DSN");
  if (dsn == nullptr) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }

  const auto client = ::drogon::orm::DbClient::newPgClient(dsn, 1);
  seed(*client);

  const auto config =
      std::make_shared<const cms::db::connection_config>(cms::db::connection_config{client});
  auto db = cms::db::connection{config};
  REQUIRE(db.is_connected());
  REQUIRE(db.ping_server());

  const auto snapshot = cms::db::settings_snapshot_query();
  auto seen_value = std::string{};
  for (const auto& row : db(snapshot)) {
    if (row.key == SETTING_KEY) {
      seen_value = std::string{row.valueText};
      CHECK(row.kind == "text");
      CHECK(row.tier == "public");
    }
  }
  CHECK(seen_value == SETTING_VALUE);

  const auto live = cms::db::live_session_query();
  auto bindings = cms::db::bindings_for(live);
  bindings.session_id = std::string{SESSION_ID};
  bindings.now = std::chrono::time_point_cast<std::chrono::microseconds>(
      std::chrono::system_clock::now());

  auto principal = std::string{};
  auto expiry = cms::db::timestamp{};
  auto revoked = std::optional<cms::db::timestamp>{cms::db::timestamp{}};
  for (const auto& row : db.select(live, bindings)) {
    principal = std::string{row.principalId};
    expiry = row.expiresAt;
    revoked = row.revokedAt;
  }
  CHECK(principal == PRINCIPAL_ID);
  CHECK(expiry == seeded_expiry());
  CHECK(!revoked.has_value());

  const auto probe =
      sqlpp::statement_t<>{} << sqlpp::parameterized_verbatim<sqlpp::integral>("SELECT ",
                                                                              sqlpp::value(1), "");
  CHECK_NOTHROW(db(probe));
}

TEST_CASE("a statement against a table that does not exist throws", "[db][connection][live]") {
  const auto* dsn = std::getenv("CMSLEAN_TEST_DSN");
  if (dsn == nullptr) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }

  const auto client = ::drogon::orm::DbClient::newPgClient(dsn, 1);
  const auto config =
      std::make_shared<const cms::db::connection_config>(cms::db::connection_config{client});
  const auto handle = cms::db::connection_handle{config};

  CHECK_THROWS((void)cms::db::run(
      handle, cms::db::serialized_statement{"SELECT 1 FROM cms_gate_missing_table", {}}));
}
