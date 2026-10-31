#include <cms/bank/seed.hpp>

#include <cstdint>
#include <exception>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <chrono>

#include <sqlpp23/sqlpp23.h>

#include <cms/db/tables.hpp>

#include "seed_curriculum.hpp"

namespace cms::bank {
namespace {

/// A task row key, so a lookup cannot pair an id with the wrong version.
using TaskKey = std::pair<std::string, std::int32_t>;

auto now_stamp() -> cms::db::timestamp {
  return std::chrono::time_point_cast<cms::db::timestamp::duration>(
      std::chrono::system_clock::now());
}

struct SeedState {
  std::int64_t version{0};
  std::string content_hash;
  bool is_present{false};
};

struct ExistingTask {
  std::string content_hash;
  bool is_seeded{false};
};

auto read_state(db::connection& database) -> SeedState {
  auto state = SeedState{};
  for (const auto& row : database(sqlpp::select(db::contentSeed.version, db::contentSeed.contentHash)
                                      .from(db::contentSeed))) {
    state.version = row.version;
    state.content_hash = std::string{row.contentHash};
    state.is_present = true;
  }
  return state;
}

auto read_tasks(db::connection& database) -> std::map<TaskKey, ExistingTask> {
  auto existing = std::map<TaskKey, ExistingTask>{};
  for (const auto& row : database(sqlpp::select(db::tasks.id, db::tasks.version,
                                                db::tasks.contentHash, db::tasks.seeded)
                                      .from(db::tasks))) {
    existing[TaskKey{std::string{row.id}, static_cast<std::int32_t>(row.version)}] =
        ExistingTask{.content_hash = std::string{row.contentHash}, .is_seeded = row.seeded};
  }
  return existing;
}

auto write_topics(db::connection& database, const Bank& bank,
                  std::map<std::string, bool>& is_present) -> std::size_t {
  auto written = std::size_t{0};
  for (const auto& topic : bank.topics) {
    if (is_present.contains(topic.id)) {
      database(sqlpp::update(db::topics)
                   .set(db::topics.labelEn = topic.label_en, db::topics.labelTh = topic.label_th)
                   .where(db::topics.id == topic.id));
    } else {
      database(sqlpp::insert_into(db::topics).set(
          db::topics.id = topic.id, db::topics.labelEn = topic.label_en,
          db::topics.labelTh = topic.label_th));
    }
    ++written;
  }
  return written;
}

/// The identity columns are the only ones a re-seed updates in place; everything derived from the files
/// is replaced, so a removed example cannot survive an edit.
auto write_task_row(db::connection& database, const Task& task, bool is_update) {
  if (is_update) {
    database(sqlpp::update(db::tasks)
                 .set(db::tasks.status = task.status, db::tasks.titleEn = task.title_en,
                      db::tasks.titleTh = task.title_th, db::tasks.difficulty = task.difficulty,
                      db::tasks.timeLimitMs = task.time_limit_ms,
                      db::tasks.memoryLimitMb = task.memory_limit_mb,
                      db::tasks.checker = task.checker_kind,
                      db::tasks.checkerPath = task.checker_path,
                      db::tasks.origin = task.origin_kind,
                      db::tasks.verification = task.verification_status,
                      db::tasks.solution = task.solution,
                      db::tasks.contentHash = task.content_hash,
                      db::tasks.seeded = true, db::tasks.seededAt = now_stamp())
                 .where(db::tasks.id == task.id && db::tasks.version == task.version));
    return;
  }
  database(sqlpp::insert_into(db::tasks).set(
      db::tasks.id = task.id, db::tasks.version = task.version, db::tasks.status = task.status,
      db::tasks.titleEn = task.title_en, db::tasks.titleTh = task.title_th,
      db::tasks.difficulty = task.difficulty, db::tasks.timeLimitMs = task.time_limit_ms,
      db::tasks.memoryLimitMb = task.memory_limit_mb, db::tasks.checker = task.checker_kind,
      db::tasks.checkerPath = task.checker_path, db::tasks.origin = task.origin_kind,
      db::tasks.verification = task.verification_status, db::tasks.solution = task.solution,
      db::tasks.contentHash = task.content_hash, db::tasks.seeded = true));
}

auto replace_children(db::connection& database, const Task& task, bool is_update) {
  if (is_update) {
    database(sqlpp::delete_from(db::taskTopics)
                 .where(db::taskTopics.taskId == task.id &&
                        db::taskTopics.taskVersion == task.version));
    database(sqlpp::delete_from(db::taskStatements)
                 .where(db::taskStatements.taskId == task.id &&
                        db::taskStatements.taskVersion == task.version));
    database(sqlpp::delete_from(db::taskExamples)
                 .where(db::taskExamples.taskId == task.id &&
                        db::taskExamples.taskVersion == task.version));
  }
  for (const auto& topic : task.topics) {
    database(sqlpp::insert_into(db::taskTopics).set(
        db::taskTopics.taskId = task.id, db::taskTopics.taskVersion = task.version,
        db::taskTopics.topic = topic));
  }
  for (const auto& [locale, body] : task.statements) {
    database(sqlpp::insert_into(db::taskStatements).set(
        db::taskStatements.taskId = task.id, db::taskStatements.taskVersion = task.version,
        db::taskStatements.locale = locale, db::taskStatements.body = body));
  }
  for (const auto& example : task.examples) {
    database(sqlpp::insert_into(db::taskExamples).set(
        db::taskExamples.taskId = task.id, db::taskExamples.taskVersion = task.version,
        db::taskExamples.ordinal = example.ordinal, db::taskExamples.input = example.input,
        db::taskExamples.expectedOutput = example.expected_output));
  }
}

auto write_state(db::connection& database, const SeedState& state, const Bank& bank) {
  const auto version = state.version + 1;
  if (state.is_present) {
    database(sqlpp::update(db::contentSeed)
                 .set(db::contentSeed.version = version,
                      db::contentSeed.contentHash = bank.content_hash,
                      db::contentSeed.seededAt = now_stamp())
                 .where(db::contentSeed.singleton == true));
    return;
  }
  database(sqlpp::insert_into(db::contentSeed).set(
      db::contentSeed.version = version, db::contentSeed.contentHash = bank.content_hash,
      db::contentSeed.seededAt = now_stamp()));
}

auto existing_topic_ids(db::connection& database) -> std::map<std::string, bool> {
  auto ids = std::map<std::string, bool>{};
  for (const auto& row : database(sqlpp::select(db::topics.id).from(db::topics))) {
    ids[std::string{row.id}] = true;
  }
  return ids;
}

/// Imports the tasks whose digest moved, leaving an administrator's row alone and counting what it did.
auto import_tasks(db::connection& database, const Bank& bank, const std::map<TaskKey, ExistingTask>& existing,
                  SeedReport& report) {
  for (const auto& task : bank.tasks) {
    const auto found = existing.find(TaskKey{task.id, task.version});
    if (found != existing.end() && !found->second.is_seeded) {
      // An administrator owns this row. Importing over it would silently discard their edit.
      ++report.tasks_kept;
      continue;
    }
    if (found != existing.end() && found->second.content_hash == task.content_hash) {
      continue;
    }
    const auto is_update = found != existing.end();
    write_task_row(database, task, is_update);
    replace_children(database, task, is_update);
    if (is_update) {
      ++report.tasks_updated;
    } else {
      ++report.tasks_inserted;
    }
  }
}

}  // namespace

auto seed(db::connection& database, const Bank& bank) -> Result<SeedReport> {
  // The curriculum is imported before the state row moves: if it fails, the transaction rolls back and
  // the previous version is still what the database reports.
  auto report = SeedReport{};
  try {
    const auto state = read_state(database);
    if (state.is_present && state.content_hash == bank.content_hash) {
      report.version = state.version;
      return report;
    }

    auto topics = existing_topic_ids(database);
    report.topics_written = write_topics(database, bank, topics);
    const auto curriculum = import_curriculum(database, bank);
    if (!curriculum) {
      return std::unexpected(curriculum.error());
    }
    report.tracks_written = *curriculum;
    import_tasks(database, bank, read_tasks(database), report);

    write_state(database, state, bank);
    report.is_changed = true;
    report.version = state.version + 1;
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }
  return report;
}

}  // namespace cms::bank
