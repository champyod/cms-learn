// A step's artefact is written through the store client and recorded with the digest of its bytes; reading it
// back checks that digest first, so bytes that changed under the row are refused instead of returned.

#include <cms/generate/artefact.hpp>
#include <cms/generate/model.hpp>
#include <cms/object_store.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <sqlpp23/sqlpp23.h>

#include <cms/db/connection.hpp>
#include <cms/db/tables.hpp>

#include "live_database.hpp"
#include "memory_store.hpp"
#include "pipeline_fixture.hpp"

namespace {

using cms::generate::Artefact;
using cms::generate::ArtefactHome;

constexpr std::int16_t PLANNER_STEP = 1;
constexpr auto CHOSEN_PLAN = "chosen-plan";
constexpr auto PROOF = "proof";
constexpr auto PROOF_BYTES = "the chosen plan is correct because the invariant holds";
constexpr auto TAMPERED_BYTES = "the chosen plan is correct because it says so";

SQLPP_CREATE_NAME_TAG(row_count);

auto call() -> cms::generate::ModelCall {
  return cms::generate::ModelCall{
      .model_id = "planner-model", .prompt_version = "planner-v1", .seed = 42, .call_index = 0};
}

auto inline_artefact(std::string text) -> Artefact {
  return Artefact{.step = PLANNER_STEP,
                  .kind = CHOSEN_PLAN,
                  .locale = {},
                  .home = ArtefactHome::row,
                  .object_key = {},
                  .bytes = std::move(text),
                  .call = call()};
}

auto store_artefact(std::string key, std::string bytes) -> Artefact {
  return Artefact{.step = PLANNER_STEP,
                  .kind = PROOF,
                  .locale = {},
                  .home = ArtefactHome::store,
                  .object_key = std::move(key),
                  .bytes = std::move(bytes),
                  .call = call()};
}

auto artefact_rows(cms::db::connection& database, std::int64_t job) -> std::int64_t {
  auto count = std::int64_t{0};
  const auto one = sqlpp::value(1);
  for (const auto& row : database(sqlpp::select(sqlpp::count(one).as(row_count))
                                      .from(cms::db::generationArtefacts)
                                      .where(cms::db::generationArtefacts.jobId == job))) {
    count = row.row_count;
  }
  return count;
}

}  // namespace

TEST_CASE("the in-memory sink addresses every artefact by its own bytes", "[generate]") {
  auto sink = cms::generate::MemoryArtefactSink{};

  const auto written = sink.record(9, {inline_artefact("chosen: segment tree"), inline_artefact("proof")});

  REQUIRE(written.has_value());
  CHECK(*written == 2);
  CHECK(sink.job_id() == 9);
  CHECK(sink.digests()[0] == cms::store::digest_of("chosen: segment tree"));
  CHECK(sink.digests()[1] == cms::store::digest_of("proof"));
}

TEST_CASE("a step that recorded no artefact is refused rather than written as nothing", "[generate]") {
  auto sink = cms::generate::MemoryArtefactSink{};

  const auto written = sink.record(1, {});

  REQUIRE(!written.has_value());
  CHECK(written.error().message.find("no artefact") != std::string::npos);
}

TEST_CASE("an inline artefact is read back only while its text matches its digest",
          "[generate][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();
  const auto job = cms::generate::test::prepare_job(database, "pipeline-inline-probe");
  auto store = cms::generate::test::MemoryStore{};
  auto sink = cms::generate::SqlArtefactSink{*cms::generate::test::live_client(), store};

  REQUIRE(sink.record(job, {inline_artefact("chosen: segment tree")}).has_value());
  const auto read = cms::generate::read_artefact(*cms::generate::test::live_client(), store, job,
                                                 PLANNER_STEP, CHOSEN_PLAN);

  REQUIRE(read.has_value());
  CHECK(read->bytes == "chosen: segment tree");
  CHECK(read->digest == cms::store::digest_of("chosen: segment tree"));
}

TEST_CASE("an inline artefact whose text no longer matches its digest is refused", "[generate][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();
  const auto job = cms::generate::test::prepare_job(database, "pipeline-inline-fault-probe");
  auto store = cms::generate::test::MemoryStore{};
  auto sink = cms::generate::SqlArtefactSink{*cms::generate::test::live_client(), store};
  REQUIRE(sink.record(job, {inline_artefact("chosen: segment tree")}).has_value());

  database.execute_text("UPDATE generation_artefacts SET digest = $1 WHERE job_id = $2",
                        {cms::store::digest_of("a different text"), job});

  const auto read = cms::generate::read_artefact(*cms::generate::test::live_client(), store, job,
                                                 PLANNER_STEP, CHOSEN_PLAN);

  REQUIRE(!read.has_value());
  CHECK(read.error().code == cms::ErrorCode::conflict);
  CHECK(read.error().message.find("does not match the digest") != std::string::npos);
}

TEST_CASE("a proof is written to the store and read back through its digest", "[generate][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();
  const auto job = cms::generate::test::prepare_job(database, "pipeline-store-probe");
  auto store = cms::generate::test::MemoryStore{};
  auto sink = cms::generate::SqlArtefactSink{*cms::generate::test::live_client(), store};
  constexpr auto KEY = "tasks/pipeline-store-probe/1/proof.md";

  const auto written = sink.record(job, {store_artefact(KEY, PROOF_BYTES)});

  REQUIRE(written.has_value());
  CHECK(store.bytes_at(KEY) == PROOF_BYTES);
  const auto read = cms::generate::read_artefact(*cms::generate::test::live_client(), store, job,
                                                 PLANNER_STEP, PROOF);
  REQUIRE(read.has_value());
  CHECK(read->bytes == PROOF_BYTES);
  CHECK(read->digest == cms::store::digest_of(PROOF_BYTES));
  CHECK(store.verified_digest() == cms::store::digest_of(PROOF_BYTES));
}

TEST_CASE("an object whose bytes changed under its row is refused, not returned", "[generate][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();
  const auto job = cms::generate::test::prepare_job(database, "pipeline-store-fault-probe");
  auto store = cms::generate::test::MemoryStore{};
  auto sink = cms::generate::SqlArtefactSink{*cms::generate::test::live_client(), store};
  constexpr auto KEY = "tasks/pipeline-store-fault-probe/1/proof.md";
  REQUIRE(sink.record(job, {store_artefact(KEY, PROOF_BYTES)}).has_value());

  store.overwrite(KEY, TAMPERED_BYTES);

  const auto read = cms::generate::read_artefact(*cms::generate::test::live_client(), store, job,
                                                 PLANNER_STEP, PROOF);

  REQUIRE(!read.has_value());
  CHECK(read.error().code == cms::ErrorCode::conflict);
  CHECK(read.error().message.find(cms::store::digest_of(PROOF_BYTES)) != std::string::npos);
  CHECK(read.error().message.find(cms::store::digest_of(TAMPERED_BYTES)) != std::string::npos);
}

TEST_CASE("a store artefact with no key is refused before anything is written", "[generate][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();
  const auto job = cms::generate::test::prepare_job(database, "pipeline-keyless-probe");
  auto store = cms::generate::test::MemoryStore{};
  auto sink = cms::generate::SqlArtefactSink{*cms::generate::test::live_client(), store};

  const auto written = sink.record(job, {store_artefact({}, PROOF_BYTES)});

  REQUIRE(!written.has_value());
  CHECK(written.error().message.find("object key") != std::string::npos);
  CHECK(artefact_rows(database, job) == 0);
}

TEST_CASE("an artefact row cannot be written without a digest", "[generate][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();
  const auto job = cms::generate::test::prepare_job(database, "pipeline-nodigest-probe");

  const auto refusal = [&] {
    try {
      database.execute_text(
          "INSERT INTO generation_artefacts (job_id, step, kind, inline_text, model_id, prompt_version, "
          "seed, call_index) VALUES ($1, 1, 'proof', 'text', 'm', 'v', 1, 0)",
          {job});
    } catch (const std::exception& failure) {
      return std::string{failure.what()};
    }
    return std::string{};
  }();

  CHECK(refusal.find("digest") != std::string::npos);
  CHECK(artefact_rows(database, job) == 0);
}

TEST_CASE("a digest that is not a content address is refused by the table", "[generate][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();
  const auto job = cms::generate::test::prepare_job(database, "pipeline-baddigest-probe");

  const auto refusal = [&] {
    try {
      database.execute_text(
          "INSERT INTO generation_artefacts (job_id, step, kind, inline_text, digest, model_id, "
          "prompt_version, seed, call_index) VALUES ($1, 1, 'proof', 'text', 'not-a-digest', 'm', 'v', 1, 0)",
          {job});
    } catch (const std::exception& failure) {
      return std::string{failure.what()};
    }
    return std::string{};
  }();

  CHECK(refusal.find("generation_artefacts_digest_check") != std::string::npos);
}
