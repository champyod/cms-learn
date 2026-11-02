#include "live_database.hpp"

#include <cms/api/settings_plan.hpp>
#include <cms/api/settings_view.hpp>
#include <cms/api/settings_write.hpp>
#include <cms/db/connection.hpp>
#include <cms/db/tables.hpp>
#include <cms/db/transaction.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sqlpp23/sqlpp23.h>

namespace {

using cms::test::has_live_database;
using cms::test::live_client;
using cms::test::live_database;

constexpr std::string_view PLANNER = "bbbbbbbb-cccc-4ddd-8eee-ffffffff0002";
constexpr std::string_view MISSING_PLAN = "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeee0009";

auto catalogue_path() -> std::string { return std::string{CMS_CATALOGUE_FILE}; }

auto shared_catalogue() -> const std::vector<cms::api::CatalogueEntry>& {
  static const auto loaded = [] {
    auto catalogue = cms::api::load_catalogue(catalogue_path());
    REQUIRE(catalogue.has_value());
    return *catalogue;
  }();
  return loaded;
}

/// The pool the apply path opens its transaction from, so the cases exercise the same entry point the
/// control socket calls.
auto live_config() -> const std::shared_ptr<const cms::db::connection_config>& {
  static const auto config = std::make_shared<const cms::db::connection_config>(
      cms::db::connection_config{.client = live_client()});
  return config;
}

/// Creates the planning principal if it is not there. Idempotent because the cases share one database and
/// Catch2 may run them in any order, and a plan names its author through a foreign key.
void given_planner() {
  auto database = live_database();
  for (const auto& row : database(sqlpp::select(cms::db::principals.id)
                                      .from(cms::db::principals)
                                      .where(cms::db::principals.id == std::string{PLANNER}))) {
    (void)row;
    return;
  }
  database(sqlpp::insert_into(cms::db::principals).set(
      cms::db::principals.id = std::string{PLANNER},
      cms::db::principals.kind = std::string{"user"},
      cms::db::principals.handle = std::string{"settings_planner"},
      cms::db::principals.status = std::string{"active"}));
}

auto stored_value(std::string_view key) -> std::optional<std::string> {
  auto database = live_database();
  for (const auto& row : database(sqlpp::select(cms::db::settings.valueText)
                                      .from(cms::db::settings)
                                      .where(cms::db::settings.key == std::string{key}))) {
    return std::string{row.valueText};
  }
  return std::nullopt;
}

auto plan_rows(std::string_view id) -> std::size_t {
  auto database = live_database();
  auto count = std::size_t{0};
  for (const auto& row : database(sqlpp::select(cms::db::settingsPlans.id)
                                      .from(cms::db::settingsPlans)
                                      .where(cms::db::settingsPlans.id == std::string{id}))) {
    (void)row;
    ++count;
  }
  return count;
}

void write_now(std::string_view key, std::string_view value) {
  auto transaction = cms::db::Transaction{*live_client()};
  const auto applied = cms::api::apply_settings(
      transaction.connection(), shared_catalogue(),
      {cms::api::SettingChange{.key = std::string{key}, .value = std::string{value}}}, PLANNER);
  REQUIRE(applied.has_value());
  transaction.commit();
}

}  // namespace

TEST_CASE("a plan reports the diff and changes no setting", "[settings][plan][live]") {
  if (!has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  given_planner();
  auto database = live_database();

  const auto preview =
      cms::api::plan_setting(database, shared_catalogue(), "site.tagline", "Planned tagline", PLANNER);
  REQUIRE(preview.has_value());
  CHECK(preview->key == "site.tagline");
  CHECK(preview->new_value == "Planned tagline");
  // No row holds the key yet, so the diff shows the value the catalogue would fall back to.
  CHECK(preview->old_value.empty());
  CHECK(preview->tier == "public");
  CHECK(preview->reload == "hot");
  CHECK(preview->id.size() == 36);
  CHECK(!preview->expires_at.empty());

  CHECK(plan_rows(preview->id) == 1);
  CHECK(!stored_value("site.tagline").has_value());
}

TEST_CASE("a plan is applied once and refuses a second apply", "[settings][plan][live]") {
  if (!has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  given_planner();
  auto database = live_database();

  const auto preview = cms::api::plan_setting(database, shared_catalogue(), "site.contact_email",
                                              "ops@example.test", PLANNER);
  REQUIRE(preview.has_value());

  const auto applied = cms::api::apply_plan(live_config(), shared_catalogue(), preview->id, PLANNER);
  REQUIRE(applied.has_value());
  CHECK(applied->key == "site.contact_email");
  CHECK(stored_value("site.contact_email") == std::optional<std::string>{"ops@example.test"});

  const auto again = cms::api::apply_plan(live_config(), shared_catalogue(), preview->id, PLANNER);
  REQUIRE(!again.has_value());
  CHECK(again.error().code == cms::ErrorCode::conflict);
  CHECK(again.error().message.find("already applied") != std::string::npos);
}

TEST_CASE("a key outside the catalogue is refused before a plan exists",
          "[settings][plan][live]") {
  if (!has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  given_planner();
  auto database = live_database();

  const auto refused =
      cms::api::plan_setting(database, shared_catalogue(), "not.a.setting", "x", PLANNER);
  REQUIRE(!refused.has_value());
  CHECK(refused.error().code == cms::ErrorCode::invalid_argument);
  CHECK(refused.error().message.find("not.a.setting") != std::string::npos);
}

TEST_CASE("a value that does not match its kind is refused before a plan exists",
          "[settings][plan][live]") {
  if (!has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  given_planner();
  auto database = live_database();

  const auto refused =
      cms::api::plan_setting(database, shared_catalogue(), "judge.concurrency", "many", PLANNER);
  REQUIRE(!refused.has_value());
  CHECK(refused.error().code == cms::ErrorCode::invalid_argument);
  CHECK(refused.error().message.find("judge.concurrency") != std::string::npos);
}

TEST_CASE("a plan whose key moved after it was made is refused", "[settings][plan][live]") {
  if (!has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  given_planner();
  auto database = live_database();

  const auto preview = cms::api::plan_setting(database, shared_catalogue(), "site.base_url",
                                              "https://planned.example.test", PLANNER);
  REQUIRE(preview.has_value());
  write_now("site.base_url", "https://later.example.test");

  const auto refused = cms::api::apply_plan(live_config(), shared_catalogue(), preview->id, PLANNER);
  REQUIRE(!refused.has_value());
  CHECK(refused.error().code == cms::ErrorCode::conflict);
  CHECK(refused.error().message.find("changed after this plan was made") != std::string::npos);
  // The later value stands: a refused plan writes nothing.
  CHECK(stored_value("site.base_url") == std::optional<std::string>{"https://later.example.test"});
}

TEST_CASE("an expired plan is refused", "[settings][plan][live]") {
  if (!has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  given_planner();
  auto database = live_database();

  const auto id = std::string{"cccccccc-dddd-4eee-8fff-00000000000e"};
  database(sqlpp::insert_into(cms::db::settingsPlans).set(
      cms::db::settingsPlans.id = id, cms::db::settingsPlans.authorId = std::string{PLANNER},
      cms::db::settingsPlans.changes =
          std::string{R"({"key":"judge.enabled","value":"true"})"},
      cms::db::settingsPlans.createdAt = cms::db::stamp_after(-3600),
      cms::db::settingsPlans.expiresAt = cms::db::stamp_after(-60)));

  const auto refused = cms::api::apply_plan(live_config(), shared_catalogue(), id, PLANNER);
  REQUIRE(!refused.has_value());
  CHECK(refused.error().code == cms::ErrorCode::conflict);
  CHECK(refused.error().message.find("expired") != std::string::npos);
  CHECK(!stored_value("judge.enabled").has_value());
}

TEST_CASE("a plan naming the value already stored is refused", "[settings][plan][live]") {
  if (!has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  given_planner();
  auto database = live_database();
  write_now("site.name", "Already This");

  const auto preview =
      cms::api::plan_setting(database, shared_catalogue(), "site.name", "Already This", PLANNER);
  REQUIRE(preview.has_value());

  const auto refused = cms::api::apply_plan(live_config(), shared_catalogue(), preview->id, PLANNER);
  REQUIRE(!refused.has_value());
  CHECK(refused.error().code == cms::ErrorCode::conflict);
  CHECK(refused.error().message.find("nothing to apply") != std::string::npos);
}

TEST_CASE("a plan for an unknown id is refused", "[settings][plan][live]") {
  if (!has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  given_planner();

  const auto refused =
      cms::api::apply_plan(live_config(), shared_catalogue(), MISSING_PLAN, PLANNER);
  REQUIRE(!refused.has_value());
  CHECK(refused.error().code == cms::ErrorCode::not_found);
  CHECK(refused.error().message.find(MISSING_PLAN) != std::string::npos);
}

TEST_CASE("a plan for a secret never shows the stored value", "[settings][plan][live]") {
  if (!has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  given_planner();
  auto database = live_database();
  write_now("mail.password", "the-previous-credential");

  const auto preview =
      cms::api::plan_setting(database, shared_catalogue(), "mail.password", "the-next-credential",
                             PLANNER);
  REQUIRE(preview.has_value());
  CHECK(preview->tier == "secret");
  CHECK(preview->old_value == std::string{cms::api::REDACTED});
  CHECK(preview->new_value == "the-next-credential");
}
