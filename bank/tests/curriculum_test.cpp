#include <cms/bank/bank.hpp>
#include <cms/bank/curriculum.hpp>
#include <cms/bank/seed.hpp>
#include <cms/db/connection.hpp>
#include <cms/db/transaction.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include <drogon/orm/DbClient.h>

namespace {

auto shipped_bank() -> std::filesystem::path {
  return std::filesystem::path{CMS_CONTENT_DIR};
}

auto has_live_database() -> bool { return std::getenv("CMSLEAN_TEST_DSN") != nullptr; }

auto live_client() -> const std::shared_ptr<::drogon::orm::DbClient>& {
  const auto* dsn = std::getenv("CMSLEAN_TEST_DSN");
  static const auto client = ::drogon::orm::DbClient::newPgClient(dsn, 4);
  return client;
}

/// One client for the whole binary: a second Drogon client in one process deadlocks its pool.
auto live_database() -> cms::db::connection {
  return cms::db::connection{std::make_shared<const cms::db::connection_config>(
      cms::db::connection_config{.client = live_client()})};
}

/// A third route, written the way an author writes one: the same shape as the two that ship, no code.
constexpr std::string_view THIRD_TRACK = R"({
  "id": "t-third",
  "title": { "en": "Third route", "th": "เส้นทางที่สาม" },
  "description": { "en": "A route that exists only in this test.", "th": "เส้นทางที่มีเฉพาะในการทดสอบนี้" },
  "stages": [
    {
      "id": "st-third-start",
      "title": { "en": "Start", "th": "เริ่ม" },
      "difficulty": { "min": 800, "max": 1200 },
      "tags": ["io", "sorting"]
    }
  ]
})";

/// Adds THIRD_TRACK to a copy of the shipped bank, so the case never edits the repository's data.
auto bank_with_third_track() -> std::filesystem::path {
  const auto scratch = std::filesystem::temp_directory_path() / "cms-bank-third-track";
  std::filesystem::remove_all(scratch);
  std::filesystem::copy(shipped_bank(), scratch, std::filesystem::copy_options::recursive);

  const auto path = scratch / "tracks.json";
  auto text = std::string{};
  {
    auto reader = std::ifstream{path};
    text.assign(std::istreambuf_iterator<char>{reader}, std::istreambuf_iterator<char>{});
  }
  const auto close = text.rfind(']');
  REQUIRE(close != std::string::npos);
  text.insert(close, ",\n" + std::string{THIRD_TRACK} + "\n");
  {
    auto writer = std::ofstream{path, std::ios::trunc};
    writer << text;
  }
  return scratch;
}

auto seed_once(const cms::bank::Bank& bank) -> cms::Result<cms::bank::SeedReport> {
  auto transaction = cms::db::Transaction{*live_client()};
  auto report = cms::bank::seed(transaction.connection(), bank);
  if (!report) {
    return report;
  }
  transaction.commit();
  return report;
}

}  // namespace

TEST_CASE("a third route is data, not code", "[curriculum][unit]") {
  const auto shipped = cms::bank::load(shipped_bank());
  REQUIRE(shipped.has_value());
  CHECK(shipped->tracks.size() == 2);

  const auto scratch = bank_with_third_track();
  const auto extended = cms::bank::load(scratch);
  std::filesystem::remove_all(scratch);

  REQUIRE(extended.has_value());
  CHECK(extended->tracks.size() == 3);
  CHECK(extended->tracks.back().id == "t-third");
  CHECK(extended->tracks.back().stages.size() == 1);
  CHECK(extended->tracks.back().stages.front().tags.size() == 2);
  // The digest covers the routes, so adding one is visible to the seeder.
  CHECK(extended->content_hash != shipped->content_hash);
}

TEST_CASE("a track with an inverted band or an unknown tag is refused", "[curriculum][unit]") {
  const auto scratch = bank_with_third_track();
  const auto path = scratch / "tracks.json";
  auto text = std::string{};
  {
    auto reader = std::ifstream{path};
    text.assign(std::istreambuf_iterator<char>{reader}, std::istreambuf_iterator<char>{});
  }

  auto inverted = text;
  const auto band = inverted.find("\"min\": 800, \"max\": 1200");
  REQUIRE(band != std::string::npos);
  inverted.replace(band, 22, "\"min\": 1200, \"max\": 800");
  {
    auto writer = std::ofstream{path, std::ios::trunc};
    writer << inverted;
  }
  const auto bad_band = cms::bank::load(scratch);
  REQUIRE(!bad_band.has_value());
  CHECK(bad_band.error().code == cms::ErrorCode::invalid_argument);

  auto unknown = text;
  const auto tag = unknown.find("\"io\", \"sorting\"");
  REQUIRE(tag != std::string::npos);
  unknown.replace(tag, 16, "\"io\", \"telepathy\"");
  {
    auto writer = std::ofstream{path, std::ios::trunc};
    writer << unknown;
  }
  const auto bad_tag = cms::bank::load(scratch);
  std::filesystem::remove_all(scratch);
  REQUIRE(!bad_tag.has_value());
  CHECK(bad_tag.error().code == cms::ErrorCode::invalid_argument);
}

TEST_CASE("the curriculum is read back in the order the data declares", "[curriculum][live]") {
  if (!has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  const auto bank = cms::bank::load(shipped_bank());
  REQUIRE(bank.has_value());
  REQUIRE(seed_once(*bank).has_value());

  auto database = live_database();
  const auto curriculum = cms::bank::read_curriculum(database);
  REQUIRE(curriculum.has_value());
  REQUIRE(curriculum->size() == bank->tracks.size());

  for (auto index = std::size_t{0}; index < bank->tracks.size(); ++index) {
    const auto& expected = bank->tracks[index];
    const auto& actual = curriculum->at(index);
    CHECK(actual.id == expected.id);
    CHECK(actual.stages.size() == expected.stages.size());
    CHECK(actual.stages.front().id == expected.stages.front().id);
    CHECK(actual.stages.front().tags.size() == expected.stages.front().tags.size());
  }
}

TEST_CASE("importing the same curriculum twice writes nothing", "[curriculum][live]") {
  if (!has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  const auto bank = cms::bank::load(shipped_bank());
  REQUIRE(bank.has_value());

  const auto first = seed_once(*bank);
  REQUIRE(first.has_value());
  const auto second = seed_once(*bank);
  REQUIRE(second.has_value());

  CHECK(!second->is_changed);
  CHECK(second->version == first->version);
  CHECK(second->tracks_written == 0);
}
