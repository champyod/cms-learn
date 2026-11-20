#include <cms/bank/seed.hpp>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <sqlpp23/sqlpp23.h>

#include <cms/db/field_advice.hpp>
#include <cms/db/tables.hpp>

#include <cms/bank/scoring.hpp>

#include "seed_curriculum.hpp"

namespace cms::bank {
namespace {

/// A task row key, so a lookup cannot pair an id with the wrong version.
using TaskKey = std::pair<std::string, std::int32_t>;

/// A seeded task starts unverified, and a re-seed leaves the column alone: the certificate row is what changes a
/// task's verification (ADR 0046), so a content edit cannot reset or raise it.
constexpr std::string_view VERIFICATION_PENDING = "pending";

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

auto optional_text(const std::string& value) -> std::optional<std::string> {
  return value.empty() ? std::nullopt : std::optional<std::string>{value};
}

/// The identity columns are the only ones a re-seed updates in place; everything derived from the files
/// is replaced, so a removed example cannot survive an edit.
/// @brief Refuses a file that asks to be published before a machine has checked it (ADR 0057).
///
/// The content gate refuses the same thing at authoring time; this refusal is for the paths that never run the gate, and
/// the trigger is what holds when a future writer bypasses both. The job is to tell the author what to run, in order.
auto refuse_published(const Task& task) -> void {
  throw std::runtime_error(
      "task " + task.id + " version " + std::to_string(task.version) +
      " asks to be published, and a published task needs a verified certificate: seed it as draft, run "
      "cms-generate --certify-content " + task.id + " to certify its reference against its examples, then publish it");
}

auto write_task_row(db::connection& database, const Task& task, bool is_update) {
  if (task.status == "published") {
    refuse_published(task);
  }
  if (is_update) {
    database(sqlpp::update(db::tasks)
                 .set(db::tasks.status = task.status,
                      db::tasks.taskType = optional_text(task.task_type),
                      db::tasks.titleEn = task.title_en,
                      db::tasks.titleTh = task.title_th, db::tasks.difficulty = task.difficulty,
                      db::tasks.timeLimitMs = task.time_limit_ms,
                      db::tasks.memoryLimitMb = task.memory_limit_mb,
                      db::tasks.checker = task.checker_kind,
                      db::tasks.checkerPath = task.checker_path,
                      db::tasks.origin = task.origin_kind,
                      db::tasks.solution = task.solution,
                      db::tasks.points = task.points,
                      db::tasks.contentHash = task.content_hash,
                      db::tasks.seeded = true, db::tasks.seededAt = db::now_stamp())
                 .where(db::tasks.id == task.id && db::tasks.version == task.version));
    return;
  }
  database(sqlpp::insert_into(db::tasks).set(
      db::tasks.id = task.id, db::tasks.version = task.version, db::tasks.status = task.status,
      db::tasks.taskType = optional_text(task.task_type),
      db::tasks.titleEn = task.title_en, db::tasks.titleTh = task.title_th,
      db::tasks.difficulty = task.difficulty, db::tasks.timeLimitMs = task.time_limit_ms,
      db::tasks.memoryLimitMb = task.memory_limit_mb, db::tasks.checker = task.checker_kind,
      db::tasks.checkerPath = task.checker_path, db::tasks.origin = task.origin_kind,
      db::tasks.verification = std::string{VERIFICATION_PENDING}, db::tasks.solution = task.solution,
      db::tasks.points = task.points, db::tasks.contentHash = task.content_hash,
      db::tasks.seeded = true));
}

/// The score mode the content declares, written where the record reads it: the subtask rules ask this row which mode
/// the task scores by (ADR 0033 §4). A task that declares none keeps whatever an administrator set.
auto write_score_mode(db::connection& database, const Task& task) -> void {
  if (task.score_mode.empty()) {
    return;
  }
  database.execute_text(
      "INSERT INTO task_field_values (task_id, task_version, field_key, value) "
      "VALUES ($1, $2, 'score_mode', to_jsonb($3::text)) "
      "ON CONFLICT (task_id, task_version, field_key) DO UPDATE SET value = excluded.value",
      {task.id, static_cast<std::int64_t>(task.version), task.score_mode});
}

/// The scoring groups are replaced as one set: a re-seed that drops a subtask must not leave the old row scoring.
auto write_subtasks(db::connection& database, const Task& task) -> void {
  database(sqlpp::delete_from(db::taskSubtasks)
               .where(db::taskSubtasks.taskId == task.id &&
                      db::taskSubtasks.taskVersion == task.version));
  for (const auto& subtask : task.subtasks) {
    database(sqlpp::insert_into(db::taskSubtasks).set(
        db::taskSubtasks.taskId = task.id, db::taskSubtasks.taskVersion = task.version,
        db::taskSubtasks.position = subtask.position, db::taskSubtasks.points = subtask.points,
        db::taskSubtasks.minN = subtask.bounds.min_n, db::taskSubtasks.maxN = subtask.bounds.max_n,
        db::taskSubtasks.valueMin = subtask.bounds.value_min,
        db::taskSubtasks.valueMax = subtask.bounds.value_max));
  }
}

/// @return The scoring groups as the rows hold them, which is what the statement's section is rendered from.
auto read_subtasks(db::connection& database, const Task& task) -> std::vector<Subtask> {
  auto subtasks = std::vector<Subtask>{};
  for (const auto& row : database(sqlpp::select(db::taskSubtasks.position, db::taskSubtasks.points,
                                                db::taskSubtasks.minN, db::taskSubtasks.maxN,
                                                db::taskSubtasks.valueMin, db::taskSubtasks.valueMax)
                                      .from(db::taskSubtasks)
                                      .where(db::taskSubtasks.taskId == task.id &&
                                             db::taskSubtasks.taskVersion == task.version)
                                      .order_by(db::taskSubtasks.position.asc()))) {
    subtasks.push_back(Subtask{.position = static_cast<std::int32_t>(row.position),
                               .points = static_cast<std::int32_t>(row.points),
                               .bounds = cms::SubtaskBounds{.min_n = row.minN,
                                                            .max_n = row.maxN,
                                                            .value_min = row.valueMin,
                                                            .value_max = row.valueMax}});
  }
  return subtasks;
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
  // The section is rendered from the rows just written, so the statement and the judge read one record of the
  // scoring (ADR 0033 §4): a subtask edited on a row cannot leave a statement describing the old points.
  const auto subtasks = read_subtasks(database, task);
  for (const auto& [locale, body] : task.statements) {
    const auto section = scoring_section(locale, subtasks, task.points);
    auto with_scoring = std::string{body};
    if (!section.empty()) {
      with_scoring.reserve(with_scoring.size() + 2 + section.size());
      with_scoring += "\n\n";
      with_scoring += section;
    }
    database(sqlpp::insert_into(db::taskStatements).set(
        db::taskStatements.taskId = task.id, db::taskStatements.taskVersion = task.version,
        db::taskStatements.locale = locale, db::taskStatements.body = with_scoring));
  }
  for (const auto& example : task.examples) {
    database(sqlpp::insert_into(db::taskExamples).set(
        db::taskExamples.taskId = task.id, db::taskExamples.taskVersion = task.version,
        db::taskExamples.ordinal = example.ordinal, db::taskExamples.input = example.input,
        db::taskExamples.expectedOutput = example.expected_output));
  }
}

/// The rung list is reconciled on every run rather than behind the content digest: levels.json is not part
/// of the bank's digest (topics, tracks and tasks are), so gating it on that digest would let an edited band
/// sit unapplied until something else changed. Each row is written only when it differs, so a run that
/// changes nothing writes nothing new.
///
/// The icon column is the rung's own id: the list carries no icon, and a shape the database already accepts
/// is better than a second invented vocabulary. The rows land one statement at a time inside the caller's
/// transaction, which is what lets the deferred partition trigger see the whole list at commit.
auto write_levels(db::connection& database, const Bank& bank) -> std::size_t {
  struct StoredLevel {
    std::int32_t position{0};
    std::int32_t difficulty_min{0};
    std::int32_t difficulty_max{0};
    std::string icon;
  };
  auto existing = std::map<std::string, StoredLevel>{};
  for (const auto& row : database(sqlpp::select(db::levels.id, db::levels.position,
                                                db::levels.difficultyMin, db::levels.difficultyMax,
                                                db::levels.icon)
                                      .from(db::levels))) {
    existing[std::string{row.id}] =
        StoredLevel{.position = static_cast<std::int32_t>(row.position),
                    .difficulty_min = static_cast<std::int32_t>(row.difficultyMin),
                    .difficulty_max = static_cast<std::int32_t>(row.difficultyMax),
                    .icon = std::string{row.icon}};
  }
  auto titles = std::map<std::pair<std::string, std::string>, std::string>{};
  for (const auto& row : database(sqlpp::select(db::levelTitles.levelId, db::levelTitles.language,
                                                db::levelTitles.title)
                                      .from(db::levelTitles))) {
    titles[{std::string{row.levelId}, std::string{row.language}}] = std::string{row.title};
  }

  auto written = std::size_t{0};
  auto position = std::int32_t{0};
  for (const auto& level : bank.levels) {
    position += 1;
    const auto stored = existing.find(level.id);
    const auto unchanged = stored != existing.end() && stored->second.position == position &&
                           stored->second.difficulty_min == level.difficulty_min &&
                           stored->second.difficulty_max == level.difficulty_max &&
                           stored->second.icon == level.id;
    if (unchanged) {
      // nothing to write for this rung
    } else if (stored != existing.end()) {
      database(sqlpp::update(db::levels)
                   .set(db::levels.position = position,
                        db::levels.difficultyMin = level.difficulty_min,
                        db::levels.difficultyMax = level.difficulty_max,
                        db::levels.icon = level.id)
                   .where(db::levels.id == level.id));
    } else {
      database(sqlpp::insert_into(db::levels).set(
          db::levels.id = level.id, db::levels.position = position,
          db::levels.difficultyMin = level.difficulty_min,
          db::levels.difficultyMax = level.difficulty_max, db::levels.icon = level.id));
    }
    for (const auto& [language, title] :
         {std::pair<std::string_view, std::string_view>{"en", level.title_en},
          {"th", level.title_th}}) {
      if (title.empty()) {
        continue;
      }
      const auto key = std::pair<std::string, std::string>{level.id, std::string{language}};
      const auto stored_title = titles.find(key);
      if (stored_title != titles.end() && stored_title->second == title) {
        continue;
      }
      if (stored_title != titles.end()) {
        database(sqlpp::update(db::levelTitles)
                     .set(db::levelTitles.title = std::string{title})
                     .where(db::levelTitles.levelId == level.id &&
                            db::levelTitles.language == std::string{language}));
      } else {
        database(sqlpp::insert_into(db::levelTitles).set(
            db::levelTitles.levelId = level.id, db::levelTitles.language = std::string{language},
            db::levelTitles.title = std::string{title}));
      }
    }
    ++written;
  }
  return written;
}

auto write_state(db::connection& database, const SeedState& state, const Bank& bank) {
  const auto version = state.version + 1;
  if (state.is_present) {
    database(sqlpp::update(db::contentSeed)
                 .set(db::contentSeed.version = version,
                      db::contentSeed.contentHash = bank.content_hash,
                      db::contentSeed.seededAt = db::now_stamp())
                 .where(db::contentSeed.singleton == true));
    return;
  }
  database(sqlpp::insert_into(db::contentSeed).set(
      db::contentSeed.version = version, db::contentSeed.contentHash = bank.content_hash,
      db::contentSeed.seededAt = db::now_stamp()));
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
    // An imported task is administrator-owned: no visibility rows is the ADR default, and a grant an
    // administrator made to a re-imported task must survive the import.
    const auto is_update = found != existing.end();
    write_task_row(database, task, is_update);
    write_score_mode(database, task);
    write_subtasks(database, task);
    replace_children(database, task, is_update);
    if (is_update) {
      ++report.tasks_updated;
    } else {
      ++report.tasks_inserted;
    }
  }
}

}  // namespace

/// The definitions are written before the digest is compared and every statement is insert-or-update, so a
/// seed that changes nothing about the bank still reconciles the type rows and a second run writes no
/// duplicate. The writes are attributed to the system account, because a definition change is an audited
/// event and "someone" is not an actor.
constexpr std::string_view SEED_TASK_TYPES_SQL = R"(
WITH actor AS (
  INSERT INTO system_accounts (id, handle) VALUES ('00000000-0000-4000-8000-000000000001', 'system:content')
  ON CONFLICT (id) DO NOTHING
  RETURNING id
)
SELECT set_config('cms.actor_kind', 'system', true), set_config('cms.invoked_by', 'seed', true),
       set_config('cms.actor_id', '00000000-0000-4000-8000-000000000001', true)
)";

/// The list as the jsonb text the column holds, or NULL when there is none: a choice with no choices is
/// refused by the table, so "none" has to be NULL and never an empty array.
auto json_or_null(const std::vector<std::string>& values) -> db::bound_value {
  if (values.empty()) {
    return std::nullptr_t{};
  }
  auto text = std::string{"["};
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0) {
      text.push_back(',');
    }
    text.push_back('"');
    text += values[index];
    text.push_back('"');
  }
  text.push_back(']');
  return text;
}

auto write_task_types(db::connection& database, const Bank& bank) -> Result<void> {
  try {
    database.execute_text(std::string{SEED_TASK_TYPES_SQL}, {});
    for (const auto& type : bank.task_types) {
      database.execute_text(
          "INSERT INTO task_types (id, title_en, title_th, position) VALUES ($1, $2, $3, $4) "
          "ON CONFLICT (id) DO UPDATE SET title_en = excluded.title_en, title_th = excluded.title_th, "
          "position = excluded.position",
          {type.id, type.title_en, type.title_th, static_cast<std::int64_t>(type.position)});
      for (const auto& field : type.fields) {
        database.execute_text(
            "INSERT INTO task_field_defs (task_type_id, key, kind, label_en, label_th, required, choices, "
            "members, minimum, maximum, position) "
            "VALUES ($1, $2, $3::task_field_kind, $4, $5, $6, $7::jsonb, $8::jsonb, $9, $10, $11) "
            "ON CONFLICT (task_type_id, key) DO UPDATE SET kind = excluded.kind, "
            "label_en = excluded.label_en, label_th = excluded.label_th, required = excluded.required, "
            "choices = excluded.choices, members = excluded.members, minimum = excluded.minimum, "
            "maximum = excluded.maximum, position = excluded.position",
            {type.id, field.key, field.kind, field.label_en, field.label_th, field.is_required,
             json_or_null(field.choices), json_or_null(field.members),
             field.minimum.has_value()
                 ? db::bound_value{static_cast<std::int64_t>(*field.minimum)}
                 : db::bound_value{std::nullptr_t{}},
             field.maximum.has_value()
                 ? db::bound_value{static_cast<std::int64_t>(*field.maximum)}
                 : db::bound_value{std::nullptr_t{}},
             static_cast<std::int64_t>(field.position)});
      }
    }
    return {};
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }
}

/// The widest artefact id a file field can hold, matching the digits the column's own trigger accepts.
constexpr std::size_t MAX_ARTEFACT_ID_DIGITS = 18;

struct StoredObject {
  std::string key;
  std::string digest;
};

/// The id @p value names, or nothing when it names no id. A file-kind value is JSON: an id is a number, while the
/// reading this column had before ADR 0027 §5 is a quoted store key. The table's trigger applies the same rule,
/// so one sentence describes the refusal wherever it happens.
auto artefact_id_in(std::string_view value) -> std::optional<std::int64_t> {
  if (value.empty() || value.size() > MAX_ARTEFACT_ID_DIGITS) {
    return std::nullopt;
  }
  auto id = std::int64_t{0};
  for (const char digit : value) {
    if (digit < '0' || digit > '9') {
      return std::nullopt;
    }
    id = (id * 10) + (digit - '0');
  }
  return id;
}

/// The artefact @p id names, as the key and digest its row records; nothing when it names no stored object. An
/// inline artefact is not one: a file field names bytes a tool reads from the store, never a row's own text.
auto read_stored_object(db::connection& database, std::int64_t id) -> std::optional<StoredObject> {
  for (const auto& row : database(sqlpp::select(db::generationArtefacts.objectKey,
                                                db::generationArtefacts.digest)
                                      .from(db::generationArtefacts)
                                      .where(db::generationArtefacts.id == id))) {
    if (!row.objectKey.has_value()) {
      return std::nullopt;
    }
    return StoredObject{.key = std::string{*row.objectKey}, .digest = std::string{row.digest}};
  }
  return std::nullopt;
}

/// Every file-kind field value names an artefact, and its bytes are read back through the digest that artefact
/// records. A value that names no artefact, or an artefact whose object is gone, is refused here before the digest
/// gate and before any write: an import that seeded a task whose statement is absent would be the empty content
/// ADR 0023 exists to prevent, and a transaction that rolls back leaves nothing half-written.
auto verify_objects(db::connection& database, const cms::store::ObjectStore* objects) -> Result<void> {
  struct NamedValue {
    std::string task_id;
    std::int64_t version{0};
    std::string field_key;
    std::string value;
  };
  auto values = std::vector<NamedValue>{};
  try {
    for (const auto& row : database(sqlpp::select(db::taskFieldValues.taskId,
                                                  db::taskFieldValues.taskVersion,
                                                  db::taskFieldValues.fieldKey,
                                                  db::taskFieldValues.value)
                                        .from(db::taskFieldValues))) {
      values.push_back(NamedValue{.task_id = std::string{row.taskId},
                                  .version = row.taskVersion,
                                  .field_key = std::string{row.fieldKey},
                                  .value = std::string{row.value}});
    }
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }

  for (const auto& value : values) {
    auto type_id = std::string{};
    for (const auto& row : database(sqlpp::select(db::tasks.taskType)
                                        .from(db::tasks)
                                        .where(db::tasks.id == value.task_id &&
                                               db::tasks.version == value.version))) {
      type_id = row.taskType.has_value() ? std::string{*row.taskType} : std::string{};
    }
    if (type_id.empty()) {
      continue;
    }
    auto kind = std::string{};
    for (const auto& row : database(sqlpp::select(db::taskFieldDefs.kind)
                                        .from(db::taskFieldDefs)
                                        .where(db::taskFieldDefs.taskTypeId == type_id &&
                                               db::taskFieldDefs.key == value.field_key))) {
      kind = std::string{row.kind};
    }
    if (kind != "file") {
      continue;
    }
    const auto subject = "the field " + value.field_key + " of task " + value.task_id + " version " +
                         std::to_string(value.version);
    const auto id = artefact_id_in(value.value);
    if (!id) {
      const auto advice = db::file_field_advice(database, value.field_key);
      if (!advice) {
        return std::unexpected(advice.error());
      }
      return std::unexpected(Error::make(
          ErrorCode::invalid_argument,
          "task " + value.task_id + " version " + std::to_string(value.version) + ": " + *advice));
    }
    const auto object = read_stored_object(database, *id);
    if (!object) {
      const auto advice = db::file_field_unknown_artefact(database, value.field_key, *id);
      if (!advice) {
        return std::unexpected(advice.error());
      }
      return std::unexpected(Error::make(
          ErrorCode::invalid_argument,
          "task " + value.task_id + " version " + std::to_string(value.version) + ": " + *advice));
    }
    if (objects == nullptr) {
      return std::unexpected(Error::make(
          ErrorCode::unavailable,
          subject + " names artefact " + std::to_string(*id) + " at " + object->key +
              ", and no object store is configured to read it"));
    }
    const auto bytes = objects->read_verified(object->key, object->digest);
    if (!bytes) {
      return std::unexpected(Error::make(
          bytes.error().code,
          subject + " names artefact " + std::to_string(*id) + ": " + bytes.error().message));
    }
  }
  return {};
}

auto seed(db::connection& database, const Bank& bank, const cms::store::ObjectStore* objects)
    -> Result<SeedReport> {
  // The curriculum is imported before the state row moves: if it fails, the transaction rolls back and
  // the previous version is still what the database reports.
  auto report = SeedReport{};
  try {
    const auto present = verify_objects(database, objects);
    if (!present) {
      return std::unexpected(present.error());
    }
    const auto types = write_task_types(database, bank);
    if (!types) {
      return std::unexpected(types.error());
    }
    write_levels(database, bank);
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
