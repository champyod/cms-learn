// The publisher's evidence is a row, so the read that finds it is proven against the schema it reads: the newest
// certificate for a task version, a failed one named as failed, and no row for a version the certifier never wrote.

#include <cms/judge/promote.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>

#include <drogon/orm/DbClient.h>
#include <sqlpp23/sqlpp23.h>

#include <cms/db/connection.hpp>
#include <cms/db/tables.hpp>

namespace {

constexpr std::string_view TASK_ID = "promote-sql-test";
constexpr std::int64_t SUPERSEDED_VERSION = 7;
constexpr std::int64_t FAILED_VERSION = 8;
constexpr std::int64_t ABSENT_VERSION = 9;

auto has_live_database() -> bool { return std::getenv("CMSLEAN_TEST_DSN") != nullptr; }

/// One client for the whole binary: a second Drogon client in one process deadlocks its pool.
auto live_client() -> const std::shared_ptr<::drogon::orm::DbClient>& {
  const auto* dsn = std::getenv("CMSLEAN_TEST_DSN");
  REQUIRE(dsn != nullptr);
  static const auto client = ::drogon::orm::DbClient::newPgClient(dsn, 2);
  return client;
}

auto live_database() -> cms::db::connection {
  return cms::db::connection{std::make_shared<const cms::db::connection_config>(
      cms::db::connection_config{.client = live_client()})};
}

auto given_task(cms::db::connection& database, std::int64_t version) -> void {
  database.execute_text(
      "INSERT INTO tasks (id, version, status, title_en, title_th, difficulty, time_limit_ms, memory_used_limit_mb, memory_reserved_limit_mb, "
      "checker, origin, verification, solution, content_hash) "
      "VALUES ($1, $2, 'draft', 'Promote probe', 'ทดสอบการเผยแพร่', 800, 1000, 256, 512, 'token', 'original', 'pending', "
      "'', '') ON CONFLICT (id, version) DO NOTHING",
      {std::string{TASK_ID}, version});
}

auto given_job(cms::db::connection& database, std::int64_t version) -> std::int64_t {
  database.execute_text("INSERT INTO generation_jobs (task_id, task_version) VALUES ($1, $2) "
                        "ON CONFLICT (task_id, task_version) DO NOTHING",
                        {std::string{TASK_ID}, version});
  auto job = std::int64_t{0};
  for (const auto& row : database(sqlpp::select(cms::db::generationJobs.id)
                                      .from(cms::db::generationJobs)
                                      .where(cms::db::generationJobs.taskId == std::string{TASK_ID} &&
                                             cms::db::generationJobs.taskVersion == version)
                                      .order_by(cms::db::generationJobs.id.desc())
                                      .limit(1))) {
    job = row.id;
  }
  return job;
}

/// One certificate row, created @p seconds_from_now so the newest is decided by the record rather than the clock.
auto given_certificate(cms::db::connection& database, std::int64_t version, std::string_view outcome,
                       std::string_view commit_id, std::int64_t seconds_from_now) -> void {
  const auto job = given_job(database, version);
  database.execute_text(
      "INSERT INTO generation_certificates (job_id, task_id, task_version, outcome, checks, commit_id, evidence, "
      "created_at) VALUES ($1, $2, $3, $4::task_verification_status, '[\"the cases read back\"]'::jsonb, $5, "
      "'{}'::jsonb, $6)",
      {job, std::string{TASK_ID}, version, std::string{outcome}, std::string{commit_id},
       cms::db::stamp_after(seconds_from_now)});
}

}  // namespace

TEST_CASE("the newest certificate for a task version is the one the publisher reads", "[promote][live]") {
  if (!has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = live_database();
  given_task(database, SUPERSEDED_VERSION);
  given_certificate(database, SUPERSEDED_VERSION, "failed", "deadbeef", -60);
  given_certificate(database, SUPERSEDED_VERSION, "verified", "a1b2c3d4", 0);
  const auto certificates = cms::judge::SqlCertificateSource{live_client()};

  const auto certificate = certificates.latest(TASK_ID, SUPERSEDED_VERSION);

  REQUIRE(certificate.has_value());
  REQUIRE(certificate->has_value());
  CHECK((*certificate)->outcome == "verified");
  CHECK((*certificate)->commit_id == "a1b2c3d4");
}

TEST_CASE("a failed certificate reads as failed, and names the run it came from", "[promote][live]") {
  if (!has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = live_database();
  given_task(database, FAILED_VERSION);
  given_certificate(database, FAILED_VERSION, "failed", "c0ffee11", 0);
  const auto certificates = cms::judge::SqlCertificateSource{live_client()};

  const auto certificate = certificates.latest(TASK_ID, FAILED_VERSION);

  REQUIRE(certificate.has_value());
  REQUIRE(certificate->has_value());
  CHECK((*certificate)->outcome == "failed");
  CHECK((*certificate)->commit_id == "c0ffee11");
}

TEST_CASE("a version with no certificate reads as nothing", "[promote][live]") {
  if (!has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = live_database();
  given_task(database, ABSENT_VERSION);
  const auto certificates = cms::judge::SqlCertificateSource{live_client()};

  const auto certificate = certificates.latest(TASK_ID, ABSENT_VERSION);

  REQUIRE(certificate.has_value());
  CHECK(!certificate->has_value());
}
