#include "live_database.hpp"

#include <cms/api/settings_view.hpp>
#include <cms/api/settings_write.hpp>
#include <cms/db/connection.hpp>
#include <cms/db/transaction.hpp>
#include <cms/db/tables.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include <sqlpp23/sqlpp23.h>

namespace {

using cms::test::has_live_database;
using cms::test::live_client;
using cms::test::live_database;

auto catalogue_path() -> std::string { return std::string{CMS_CATALOGUE_FILE}; }

/// The generated catalogue cannot change while the binary runs, so it is parsed once: a case that re-read it
/// would parse 108 entries for the answer the previous case already had.
auto shared_catalogue() -> const std::vector<cms::api::CatalogueEntry>& {
  static const auto loaded = [] {
    auto catalogue = cms::api::load_catalogue(catalogue_path());
    REQUIRE(catalogue.has_value());
    return *catalogue;
  }();
  return loaded;
}

/// One view for the binary: it rebuilds the visible list only when the snapshot version changes.
auto shared_view() -> const cms::api::SettingsView& {
  static const auto view = cms::api::SettingsView{shared_catalogue()};
  return view;
}

constexpr std::string_view OPERATOR = "bbbbbbbb-cccc-4ddd-8eee-ffffffff0001";

/// Creates the acting principal if it is not there. Idempotent because the cases share one database and Catch2
/// may run them in any order: a case that writes must not depend on another case having gone first.
void given_operator() {
  auto database = live_database();
  for (const auto& row : database(sqlpp::select(cms::db::principals.id)
                                      .from(cms::db::principals)
                                      .where(cms::db::principals.id == std::string{OPERATOR}))) {
    (void)row;
    return;
  }
  database(sqlpp::insert_into(cms::db::principals).set(
      cms::db::principals.id = std::string{OPERATOR}, cms::db::principals.kind = std::string{"user"},
      cms::db::principals.handle = std::string{"settings_writer"},
      cms::db::principals.status = std::string{"active"}));
}

auto stored_settings() -> cms::SettingsSnapshot {
  auto snapshot = cms::SettingsSnapshot{1, {}};
  auto database = live_database();
  for (const auto& row :
       database(sqlpp::select(cms::db::settings.key, cms::db::settings.valueText)
                    .from(cms::db::settings))) {
    snapshot.values.emplace(std::string{row.key}, std::string{row.valueText});
  }
  return snapshot;
}

auto visible(const std::string& key) -> cms::api::VisibleSetting {
  for (const auto& setting : *shared_view().for_snapshot(stored_settings())) {
    if (setting.key == key) {
      return setting;
    }
  }
  return {};
}

}  // namespace

TEST_CASE("a catalogued setting is written and read back", "[settings][write][live]") {
  if (!has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  given_operator();
  auto transaction = cms::db::Transaction{*live_client()};
  const auto applied = cms::api::apply_settings(
      transaction.connection(), shared_catalogue(),
      {cms::api::SettingChange{.key = "site.name", .value = "CMS Learn Probe"}}, OPERATOR);
  REQUIRE(applied.has_value());
  transaction.commit();

  CHECK(visible("site.name").value == "CMS Learn Probe");
}

TEST_CASE("a secret is stored and never read back", "[settings][write][live]") {
  if (!has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  const auto catalogue = cms::api::load_catalogue(catalogue_path());
  REQUIRE(catalogue.has_value());
  given_operator();

  auto transaction = cms::db::Transaction{*live_client()};
  const auto applied = cms::api::apply_settings(
      transaction.connection(), *catalogue,
      {cms::api::SettingChange{.key = "mail.password", .value = "hunter2"}}, OPERATOR);
  REQUIRE(applied.has_value());
  transaction.commit();

  // The value reached the table — that is how a credential is rotated — and no reader may see it.
  const auto shown = visible("mail.password");
  CHECK(shown.tier == "secret");
  CHECK(shown.value == std::string{cms::api::REDACTED});

  for (const auto& setting : *shared_view().for_snapshot(stored_settings())) {
    CHECK(setting.value != "hunter2");
  }
}

TEST_CASE("a key outside the catalogue is refused", "[settings][write][live]") {
  if (!has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  given_operator();

  auto database = live_database();
  const auto refused = cms::api::apply_settings(
      database, shared_catalogue(), {cms::api::SettingChange{.key = "not.a.setting", .value = "x"}},
      OPERATOR);
  REQUIRE(!refused.has_value());
  CHECK(refused.error().code == cms::ErrorCode::invalid_argument);
}

TEST_CASE("a value that does not match its kind is refused before anything is written",
          "[settings][write][live]") {
  if (!has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  given_operator();

  auto database = live_database();
  const auto before = visible("site.name").value;
  const auto refused = cms::api::apply_settings(
      database, shared_catalogue(),
      {cms::api::SettingChange{.key = "site.name", .value = "written first"},
       cms::api::SettingChange{.key = "judge.enabled", .value = "yes"}},
      OPERATOR);
  REQUIRE(!refused.has_value());
  CHECK(refused.error().message.find("judge.enabled") != std::string::npos);
  // The valid row in the same request was not applied: validation happens before any write.
  CHECK(visible("site.name").value == before);
}
