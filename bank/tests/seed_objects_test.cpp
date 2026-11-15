// A file-kind field names an artefact, and the seeder is the last reader that could still mistake it for a store
// key. Each case drives seed() against a real database and an in-memory store: the bytes are read back through the
// digest the artefact row records, so a value naming nothing is a refusal rather than a task seeded with empty
// content (ADR 0023 §2, ADR 0027 §5).

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>

#include <cms/object_store.hpp>

#include "seed_objects_fixture.hpp"

namespace {

namespace fixture = cms::bank::test;

}  // namespace

TEST_CASE("a file field naming an artefact id is resolved and checked against its digest",
          "[seed][live]") {
  if (!fixture::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = fixture::live_database();
  const auto probe = fixture::prepare(database, 1);
  auto store = fixture::MemoryStore{};
  const auto bytes = std::string{"int main() { return 0; }"};
  store.put(fixture::OBJECT_KEY, bytes);
  const auto artefact =
      fixture::add_artefact(database, probe, fixture::OBJECT_KEY, cms::store::digest_of(bytes));
  fixture::set_field_value(database, artefact);

  const auto report = fixture::seed_once(fixture::bank_or_fail(), &store);

  REQUIRE(report.has_value());
  CHECK(store.verified_digest() == cms::store::digest_of(bytes));
}

TEST_CASE("an artefact id whose object is gone refuses the import and writes nothing", "[seed][live]") {
  if (!fixture::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = fixture::live_database();
  const auto probe = fixture::prepare(database, 2);
  auto store = fixture::MemoryStore{};
  const auto artefact =
      fixture::add_artefact(database, probe, fixture::OBJECT_KEY, cms::store::digest_of("int main() {}"));
  fixture::set_field_value(database, artefact);
  const auto before = fixture::seed_state(database);
  const auto tasks_before = fixture::count_tasks(database);

  const auto report = fixture::seed_once(fixture::bank_or_fail(), &store);

  REQUIRE(!report.has_value());
  CHECK(fixture::names_field_and_id(report.error(), artefact));
  CHECK(report.error().message.find("no object at key ") != std::string::npos);
  CHECK(fixture::seed_state(database).version == before.version);
  CHECK(fixture::count_tasks(database) == tasks_before);
}

TEST_CASE("an id naming no stored object is refused by name", "[seed][live]") {
  if (!fixture::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = fixture::live_database();
  const auto probe = fixture::prepare(database, 3);
  auto store = fixture::MemoryStore{};
  const auto artefact =
      fixture::add_artefact(database, probe, fixture::OBJECT_KEY, cms::store::digest_of("int main() {}"));
  fixture::set_field_value(database, artefact);
  database.execute_text("DELETE FROM generation_artefacts WHERE id = $1", {artefact});
  const auto before = fixture::seed_state(database);

  const auto report = fixture::seed_once(fixture::bank_or_fail(), &store);

  REQUIRE(!report.has_value());
  CHECK(fixture::names_field_and_id(report.error(), artefact));
  CHECK(report.error().message.find("not a stored object") != std::string::npos);
  CHECK(fixture::seed_state(database).version == before.version);
}

TEST_CASE("an inline artefact is not a stored object for a file field", "[seed][live]") {
  if (!fixture::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = fixture::live_database();
  const auto probe = fixture::prepare(database, 4);
  auto store = fixture::MemoryStore{};
  const auto artefact = fixture::add_inline_artefact(database, probe, cms::store::digest_of("int main() {}"));
  database.execute_text("ALTER TABLE task_field_values DISABLE TRIGGER task_field_values_artefact_id",
                        {});
  fixture::set_field_value(database, artefact);
  database.execute_text("ALTER TABLE task_field_values ENABLE TRIGGER task_field_values_artefact_id",
                        {});

  const auto report = fixture::seed_once(fixture::bank_or_fail(), &store);

  REQUIRE(!report.has_value());
  CHECK(fixture::names_field_and_id(report.error(), artefact));
  CHECK(report.error().message.find("not a stored object") != std::string::npos);
  CHECK(store.verified_digest().empty());
}

TEST_CASE("a file field still holding a store key is refused by name, not skipped", "[seed][live]") {
  if (!fixture::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = fixture::live_database();
  fixture::prepare(database, 5);
  auto store = fixture::MemoryStore{};
  // The object is there: a seeder that still read the value as a key would import and call this a success.
  store.put(fixture::OBJECT_KEY, "int main() {}");
  fixture::set_legacy_key_value(database, fixture::OBJECT_KEY);
  const auto before = fixture::seed_state(database);

  const auto report = fixture::seed_once(fixture::bank_or_fail(), &store);

  REQUIRE(!report.has_value());
  CHECK(report.error().message.find("the field solution needs the id of a stored artefact") != std::string::npos);
  CHECK(report.error().message.find("the generation pipeline writes that id") != std::string::npos);
  CHECK(report.error().message.find(std::string{fixture::OBJECT_KEY}) == std::string::npos);
  CHECK(fixture::seed_state(database).version == before.version);
  CHECK(store.verified_digest().empty());
}

TEST_CASE("bytes that no longer hash to their address are refused", "[seed][live]") {
  if (!fixture::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = fixture::live_database();
  const auto probe = fixture::prepare(database, 6);
  auto store = fixture::MemoryStore{};
  store.put(fixture::OBJECT_KEY, "int main() { return 1; }");
  const auto artefact =
      fixture::add_artefact(database, probe, fixture::OBJECT_KEY, cms::store::digest_of("int main() {}"));
  fixture::set_field_value(database, artefact);

  const auto report = fixture::seed_once(fixture::bank_or_fail(), &store);

  REQUIRE(!report.has_value());
  CHECK(fixture::names_field_and_id(report.error(), artefact));
  CHECK(report.error().message.find("does not match the digest") != std::string::npos);
}

TEST_CASE("a file field naming an artefact with no store configured is refused", "[seed][live]") {
  if (!fixture::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = fixture::live_database();
  const auto probe = fixture::prepare(database, 7);
  const auto artefact =
      fixture::add_artefact(database, probe, fixture::OBJECT_KEY, cms::store::digest_of("int main() {}"));
  fixture::set_field_value(database, artefact);

  const auto report = fixture::seed_once(fixture::bank_or_fail(), nullptr);

  REQUIRE(!report.has_value());
  CHECK(fixture::names_field_and_id(report.error(), artefact));
  CHECK(report.error().message.find("no object store is configured") != std::string::npos);
}
