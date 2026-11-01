#include <cms/api/settings_view.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace {

auto catalogue_path() -> std::string { return std::string{CMS_CATALOGUE_FILE}; }

/// Loaded once for the whole binary. The catalogue is a generated file that cannot change while the test
/// runs, so reading it per case would be four parses of the same 101 entries and four copies of the same
/// snapshot for no gain.
struct Fixture {
  std::vector<cms::api::CatalogueEntry> catalogue;
  cms::SettingsSnapshot snapshot;
  std::vector<cms::api::VisibleSetting> visible;
};

auto fixture() -> const Fixture& {
  static const auto shared = [] {
    auto built = Fixture{};
    auto catalogue = cms::api::load_catalogue(catalogue_path());
    if (catalogue) {
      built.catalogue = std::move(*catalogue);
    }
    built.snapshot.values.emplace("backup.offsite_secret_access_key", std::string{"hunter2"});
    built.snapshot.values.emplace("site.name", std::string{"CMS Learn"});
    built.snapshot.values.emplace("judge.time_limit_ms", std::int64_t{1000});
    built.snapshot.values.emplace("judge.enabled", true);
    built.visible = cms::api::visible_settings(built.catalogue, built.snapshot);
    return built;
  }();
  return shared;
}

auto find(std::string_view key) -> cms::api::VisibleSetting {
  for (const auto& setting : fixture().visible) {
    if (setting.key == key) {
      return setting;
    }
  }
  return {};
}

}  // namespace

TEST_CASE("the catalogue loads and covers the document", "[settings][unit]") {
  REQUIRE_FALSE(fixture().catalogue.empty());
  CHECK(fixture().catalogue.size() > 90);
  CHECK(cms::api::is_catalogued(fixture().catalogue, "site.name"));
  CHECK_FALSE(cms::api::is_catalogued(fixture().catalogue, "unknown.key"));
}

TEST_CASE("a secret's value never reaches a reader", "[settings][unit]") {
  const auto secret = find("backup.offsite_secret_access_key");
  CHECK(secret.tier == "secret");
  CHECK(secret.value == std::string{cms::api::REDACTED});
  CHECK(secret.value != "hunter2");

  // Every secret in the catalogue, not just the one this snapshot holds: the rule is the tier, not the key.
  for (const auto& setting : fixture().visible) {
    if (setting.tier == "secret") {
      CHECK(setting.value == std::string{cms::api::REDACTED});
    }
  }
}

TEST_CASE("an ordinary setting shows the value it was given", "[settings][unit]") {
  CHECK(find("site.name").value == "CMS Learn");
  CHECK(find("judge.time_limit_ms").value == "1000");
  CHECK(find("judge.enabled").value == "true");
}

TEST_CASE("a key the build does not know is not offered", "[settings][unit]") {
  CHECK(find("unknown.key").key.empty());
}
