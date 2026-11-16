#pragma once

// The fixture a seeder case needs: one task type of the probe's own, a file-kind field on it, one draft task of
// that type, and a store that keeps its objects in memory so the bytes are checked with no network (ADR 0023 §2).

#include <cms/bank/bank.hpp>
#include <cms/bank/seed.hpp>
#include <cms/db/connection.hpp>
#include <cms/db/tables.hpp>
#include <cms/db/transaction.hpp>
#include <cms/object_store.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>

#include <drogon/orm/DbClient.h>
#include <sqlpp23/sqlpp23.h>

namespace cms::bank::test {

inline constexpr std::string_view ACTOR = "a0000001-1111-4111-8111-0000000000ce";
inline constexpr std::string_view TYPE_ID = "probe_seed_type";
inline constexpr std::string_view TASK_ID = "probe-seed-object";
inline constexpr std::string_view FIELD_KEY = "solution";
inline constexpr std::string_view OBJECT_KEY = "tasks/probe-seed/solution.cpp";
inline constexpr std::string_view MODEL_ID = "probe-model";
inline constexpr std::string_view PROMPT_VERSION = "probe-v1";
inline constexpr std::int64_t STEP = 5;

/// A store with no network, answering the same contract the client does: a missing key is `not_found` naming it,
/// and a verifying read hashes what it holds before returning it.
class MemoryStore final : public cms::store::ObjectStore {
 public:
  [[nodiscard]] auto read(std::string_view key) const -> cms::Result<std::string> override {
    const auto found = objects_.find(std::string{key});
    if (found == objects_.end()) {
      return std::unexpected(
          cms::Error::make(cms::ErrorCode::not_found, "no object at key " + std::string{key}));
    }
    return found->second;
  }

  [[nodiscard]] auto read_verified(std::string_view key, std::string_view digest) const
      -> cms::Result<std::string> override {
    verified_digest_ = std::string{digest};
    const auto bytes = read(key);
    if (!bytes) {
      return bytes;
    }
    const auto stored = cms::store::digest_of(*bytes);
    if (stored != digest) {
      return std::unexpected(cms::Error::make(
          cms::ErrorCode::conflict, "the object at key " + std::string{key} +
                                        " does not match the digest its row records: the row says " +
                                        std::string{digest} + ", the stored bytes hash to " + stored));
    }
    return bytes;
  }

  [[nodiscard]] auto write(std::string_view key, std::string_view bytes)
      -> cms::Result<std::string> override {
    objects_[std::string{key}] = std::string{bytes};
    return cms::store::digest_of(bytes);
  }

  auto put(std::string_view key, std::string_view bytes) -> void {
    objects_[std::string{key}] = std::string{bytes};
  }

  [[nodiscard]] auto verified_digest() const -> const std::string& { return verified_digest_; }

 private:
  std::map<std::string, std::string> objects_;
  mutable std::string verified_digest_;
};

struct SeedState {
  bool is_present{false};
  std::int64_t version{0};
};

[[nodiscard]] inline auto has_live_database() -> bool {
  return std::getenv("CMSLEAN_TEST_DSN") != nullptr;
}

/// One client for the whole binary: a second Drogon client in one process deadlocks its pool.
[[nodiscard]] inline auto live_client() -> const std::shared_ptr<::drogon::orm::DbClient>& {
  const auto* dsn = std::getenv("CMSLEAN_TEST_DSN");
  REQUIRE(dsn != nullptr);
  static const auto client = ::drogon::orm::DbClient::newPgClient(dsn, 4);
  return client;
}

[[nodiscard]] inline auto live_database() -> cms::db::connection {
  return cms::db::connection{std::make_shared<const cms::db::connection_config>(
      cms::db::connection_config{.client = live_client()})};
}

/// The plan's version, so a case can prove a refused import wrote nothing at all.
[[nodiscard]] inline auto seed_state(cms::db::connection& database) -> SeedState {
  auto state = SeedState{};
  for (const auto& row : database(sqlpp::select(cms::db::contentSeed.version).from(cms::db::contentSeed))) {
    state.version = row.version;
    state.is_present = true;
  }
  return state;
}

[[nodiscard]] inline auto count_tasks(cms::db::connection& database) -> std::size_t {
  auto total = std::size_t{0};
  for (const auto& row : database(sqlpp::select(cms::db::tasks.id).from(cms::db::tasks))) {
    (void)row;
    ++total;
  }
  return total;
}

/// The actor every definition write here names: a task type change is an audit event, and the tables refuse one
/// that does not say who made it. The settings are transaction-local, so the caller must have opened the
/// transaction the writes run in.
inline auto name_the_actor(cms::db::connection& database) -> void {
  database.execute_text("SELECT set_config('cms.actor_id', $1, true)", {std::string{ACTOR}});
  database.execute_text("SELECT set_config('cms.actor_kind', 'admin', true)", {});
  database.execute_text("SELECT set_config('cms.invoked_by', 'seed_objects_test', true)", {});
  database.execute_text("INSERT INTO admin_accounts (id, handle, status) VALUES ($1::uuid, "
                        "'seed_objects_probe', 'active') ON CONFLICT (id) DO NOTHING",
                        {std::string{ACTOR}});
}

/// One case's call on the probe task. The index is the case's own, because ctest runs every case as its own
/// process: a counter would start at zero in each of them and every case would claim the same artefact row.
struct Probe {
  std::int64_t call_index{0};
};

/// One task type of the probe's own, a file-kind field on it, and one draft task of that type.
///
/// The writes run in a transaction of their own: the audit trigger reads its actor from a transaction-local
/// setting, and a pooled connection is free to put that setting and the insert that needs it on two different
/// sessions — which is exactly the flake this fixture had. The connection argument names the database under test;
/// what makes the setting and the write meet is the transaction opened below. @p call_index names the case's own
/// call, so the artefact it writes is its own row.
inline auto prepare(cms::db::connection&, std::int64_t call_index) -> Probe {
  auto transaction = cms::db::Transaction{*live_client()};
  auto& database = transaction.connection();
  name_the_actor(database);
  database.execute_text(
      "INSERT INTO task_types (id, title_en, position) VALUES ($1, 'Probe', 900) "
      "ON CONFLICT (id) DO NOTHING",
      {std::string{TYPE_ID}});
  database.execute_text(
      "INSERT INTO task_field_defs (task_type_id, key, kind, label_en, position) "
      "VALUES ($1, 'solution', 'file', 'Reference solution', 1) ON CONFLICT DO NOTHING",
      {std::string{TYPE_ID}});
  database.execute_text(
      "INSERT INTO tasks (id, version, task_type, status, title_en, title_th, difficulty, time_limit_ms, "
      "memory_limit_mb, checker, origin, verification, solution, content_hash, seeded) "
      "VALUES ($1, 1, $2, 'draft', 'Probe', 'ทดสอบ', 900, 1000, 256, 'token', 'original', 'pending', '', "
      "repeat('b', 64), false) ON CONFLICT DO NOTHING",
      {std::string{TASK_ID}, std::string{TYPE_ID}});
  transaction.commit();
  return Probe{.call_index = call_index};
}

/// The row the probe wrote: found by its call identity, not by "the newest id". A database that still holds
/// another case's rows would otherwise hand this case the wrong artefact.
[[nodiscard]] inline auto artefact_of(cms::db::connection& database, std::int64_t job,
                                      const Probe& probe) -> std::int64_t {
  auto id = std::int64_t{0};
  for (const auto& row : database(sqlpp::select(cms::db::generationArtefacts.id)
                                      .from(cms::db::generationArtefacts)
                                      .where(cms::db::generationArtefacts.jobId == job &&
                                             cms::db::generationArtefacts.step == STEP &&
                                             cms::db::generationArtefacts.kind == std::string{"solution"} &&
                                             cms::db::generationArtefacts.locale.is_null() &&
                                             cms::db::generationArtefacts.callIndex == probe.call_index))) {
    id = row.id;
  }
  return id;
}

[[nodiscard]] inline auto last_job(cms::db::connection& database) -> std::int64_t {
  auto job = std::int64_t{0};
  for (const auto& row : database(sqlpp::select(cms::db::generationJobs.id)
                                      .from(cms::db::generationJobs)
                                      .where(cms::db::generationJobs.taskId == std::string{TASK_ID})
                                      .order_by(cms::db::generationJobs.id.desc())
                                      .limit(1))) {
    job = row.id;
  }
  return job;
}

/// The probe's one generation job, created the first time a case asks for it. A task version has exactly one job —
/// the schema refuses a second — so the fixture reuses the row its cases share instead of inserting one per call.
[[nodiscard]] inline auto insert_job(cms::db::connection& database) -> std::int64_t {
  const auto existing = last_job(database);
  if (existing != 0) {
    return existing;
  }
  database.execute_text("INSERT INTO generation_jobs (task_id, task_version) VALUES ($1, 1)",
                        {std::string{TASK_ID}});
  return last_job(database);
}

/// An artefact a step wrote to the store, as the row records it: the key, the digest of the bytes, and the call.
inline auto add_artefact(cms::db::connection& database, const Probe& probe, std::string_view key,
                         std::string_view digest) -> std::int64_t {
  const auto job = insert_job(database);
  database.execute_text(
      "INSERT INTO generation_artefacts (job_id, step, kind, object_key, digest, model_id, prompt_version, "
      "seed, call_index) VALUES ($1, $2, 'solution', $3, $4, $5, $6, 1, $7) "
      "ON CONFLICT (job_id, step, kind, locale, call_index) DO UPDATE "
      "SET object_key = excluded.object_key, inline_text = NULL, digest = excluded.digest, "
      "model_id = excluded.model_id, prompt_version = excluded.prompt_version, seed = excluded.seed",
      {job, STEP, std::string{key}, std::string{digest}, std::string{MODEL_ID},
       std::string{PROMPT_VERSION}, probe.call_index});
  return artefact_of(database, job, probe);
}

/// An artefact whose bytes are its row's own text: a file field names bytes a tool reads from the store, not this.
inline auto add_inline_artefact(cms::db::connection& database, const Probe& probe,
                                std::string_view digest) -> std::int64_t {
  const auto job = insert_job(database);
  database.execute_text(
      "INSERT INTO generation_artefacts (job_id, step, kind, inline_text, digest, model_id, prompt_version, "
      "seed, call_index) VALUES ($1, $2, 'solution', 'int main() {}', $3, $4, $5, 1, $6) "
      "ON CONFLICT (job_id, step, kind, locale, call_index) DO UPDATE "
      "SET inline_text = excluded.inline_text, object_key = NULL, digest = excluded.digest, "
      "model_id = excluded.model_id, prompt_version = excluded.prompt_version, seed = excluded.seed",
      {job, STEP, std::string{digest}, std::string{MODEL_ID}, std::string{PROMPT_VERSION},
       probe.call_index});
  return artefact_of(database, job, probe);
}

inline auto set_field_value(cms::db::connection& database, std::int64_t artefact_id) -> void {
  database.execute_text(
      "INSERT INTO task_field_values (task_id, task_version, field_key, value) VALUES ($1, 1, 'solution', "
      "to_jsonb($2::bigint)) ON CONFLICT (task_id, task_version, field_key) DO UPDATE SET value = excluded.value",
      {std::string{TASK_ID}, artefact_id});
}

/// A row holding a store key can only come from a database written before the column's guard existed, so the
/// fixture writes it the only way such a row can exist: with that guard switched off for one statement.
inline auto set_legacy_key_value(cms::db::connection& database, std::string_view key) -> void {
  database.execute_text(
      "ALTER TABLE task_field_values DISABLE TRIGGER task_field_values_artefact_id", {});
  database.execute_text(
      "INSERT INTO task_field_values (task_id, task_version, field_key, value) VALUES ($1, 1, 'solution', "
      "to_jsonb($2::text)) ON CONFLICT (task_id, task_version, field_key) DO UPDATE SET value = excluded.value",
      {std::string{TASK_ID}, std::string{key}});
  database.execute_text(
      "ALTER TABLE task_field_values ENABLE TRIGGER task_field_values_artefact_id", {});
}

/// Seeds the shipped bank in a transaction of the caller's making: a refusal commits nothing, which is how a case
/// proves the import wrote nothing rather than merely reported one.
[[nodiscard]] inline auto seed_once(const cms::bank::Bank& bank, const cms::store::ObjectStore* objects)
    -> cms::Result<cms::bank::SeedReport> {
  auto transaction = cms::db::Transaction{*live_client()};
  auto report = cms::bank::seed(transaction.connection(), bank, objects);
  if (!report) {
    return report;
  }
  transaction.commit();
  return report;
}

/// The bank that ships with the image, required: a case with no fixture content has nothing to import.
[[nodiscard]] inline auto bank_or_fail() -> const cms::bank::Bank& {
  static const auto bank = cms::bank::load(std::filesystem::path{CMS_CONTENT_DIR});
  REQUIRE(bank.has_value());
  return *bank;
}

[[nodiscard]] inline auto names_field_and_id(const cms::Error& error, std::int64_t artefact_id) -> bool {
  return error.message.find("the field solution") != std::string::npos &&
         error.message.find(std::to_string(artefact_id)) != std::string::npos;
}

/// @brief Whether @p error carries the seeder's sentence for an id that names no stored object, in full: the
/// location prefix, the id, the field and the next step. A looser check would stay green through a rewrite.
[[nodiscard]] inline auto says_unknown_artefact(const cms::Error& error, std::int64_t artefact_id) -> bool {
  const auto expected = "task " + std::string{TASK_ID} + " version 1: artefact " +
                        std::to_string(artefact_id) + " is not a stored object the field " +
                        std::string{FIELD_KEY} + " can use: it is either not recorded or recorded without an "
                                                 "object; the generation pipeline stores the file and records its "
                                                 "id, so store the file there first, then save the draft again";
  return error.message == expected;
}

}  // namespace cms::bank::test
