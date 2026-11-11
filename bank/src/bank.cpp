#include <cms/bank/bank.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <simdjson.h>

#include "detail.hpp"
#include "task.hpp"
#include "tracks.hpp"

namespace cms::bank {
namespace {

using detail::Hasher;
using detail::required_text;
using detail::with_json;

constexpr std::string_view TOPICS_FILE = "topics.json";
constexpr std::string_view TASK_TYPES_FILE = "task_types.json";
constexpr std::string_view TASKS_DIR = "tasks";
constexpr std::string_view TRACKS_FILE = "tracks.json";
constexpr std::string_view LEVELS_FILE = "levels.json";
constexpr std::int32_t BAND_MINIMUM = 800;
constexpr std::int32_t BAND_MAXIMUM = 3500;
constexpr std::int32_t BAND_STEP = 100;

/// The rung list, checked the way the database will check it: a list that does not partition the scale is
/// refused here, naming the file, rather than failing later on a deferred trigger that names no file.
auto load_levels(const std::filesystem::path& path) -> Result<std::vector<Level>> {
  auto levels = std::vector<Level>{};
  const auto where = path.string();
  const auto parsed = with_json(path, [&](const simdjson::dom::object& object) {
    auto array = simdjson::dom::array{};
    if (object.at_key("levels").get_array().get(array) != simdjson::SUCCESS) {
      return Result<void>{std::unexpected(Error::make(
          ErrorCode::invalid_argument, where + ": levels is missing"))};
    }
    for (const auto element : array) {
      auto entry = simdjson::dom::object{};
      if (element.get_object().get(entry) != simdjson::SUCCESS) {
        return Result<void>{std::unexpected(Error::make(
            ErrorCode::invalid_argument, where + ": a level is not an object"))};
      }
      const auto id = required_text(entry, "id", where);
      if (!id) {
        return Result<void>{std::unexpected(id.error())};
      }
      const auto title = detail::nested(entry, "title");
      if (!title) {
        return Result<void>{std::unexpected(detail::fail(where, *id + ": title is missing"))};
      }
      const auto english = required_text(*title, "en", where);
      if (!english) {
        return Result<void>{std::unexpected(english.error())};
      }
      const auto thai = detail::field_text(*title, "th").value_or(std::string{});
      const auto minimum = detail::required_integer(entry, "min", where);
      const auto maximum = detail::required_integer(entry, "max", where);
      if (!minimum || !maximum) {
        return Result<void>{std::unexpected(
            detail::fail(where, *id + ": min and max are both required"))};
      }
      levels.push_back(Level{.id = *id,
                             .title_en = *english,
                             .title_th = thai,
                             .difficulty_min = static_cast<std::int32_t>(*minimum),
                             .difficulty_max = static_cast<std::int32_t>(*maximum)});
    }
    return Result<void>{};
  });
  if (!parsed) {
    return std::unexpected(parsed.error());
  }
  std::ranges::sort(levels, {}, &Level::difficulty_min);

  auto expected = BAND_MINIMUM;
  for (const auto& level : levels) {
    if (level.difficulty_min != expected) {
      return std::unexpected(detail::fail(
          where, level.id + ": the list is not a partition of 800..3500; this band starts at " +
                     std::to_string(level.difficulty_min) + ", expected " +
                     std::to_string(expected)));
    }
    if (level.difficulty_max < level.difficulty_min || level.difficulty_max > BAND_MAXIMUM ||
        level.difficulty_min % BAND_STEP != 0 || level.difficulty_max % BAND_STEP != 0) {
      return std::unexpected(
          detail::fail(where, level.id + ": the band is not on the 800-3500 scale, step 100"));
    }
    expected = level.difficulty_max + BAND_STEP;
  }
  if (levels.empty() || expected - BAND_STEP != BAND_MAXIMUM) {
    return std::unexpected(detail::fail(
        where, "the level list does not end at 3500; the last band must finish the scale"));
  }
  return levels;
}

auto load_topics(const std::filesystem::path& path) -> Result<std::vector<Topic>> {
  auto topics = std::vector<Topic>{};
  const auto parsed = with_json(path, [&](const simdjson::dom::object& object) {
    auto array = simdjson::dom::array{};
    if (object.at_key("topics").get_array().get(array) != simdjson::SUCCESS) {
      return Result<void>{std::unexpected(
          Error::make(ErrorCode::invalid_argument, path.string() + ": topics is missing"))};
    }
    for (const auto element : array) {
      auto entry = simdjson::dom::object{};
      if (element.get_object().get(entry) != simdjson::SUCCESS) {
        return Result<void>{std::unexpected(
            Error::make(ErrorCode::invalid_argument, path.string() + ": a topic is not an object"))};
      }
      const auto id = required_text(entry, "id", path.string());
      if (!id) {
        return Result<void>{std::unexpected(id.error())};
      }
      const auto english = required_text(entry, "en", path.string());
      if (!english) {
        return Result<void>{std::unexpected(english.error())};
      }
      const auto thai = required_text(entry, "th", path.string());
      if (!thai) {
        return Result<void>{std::unexpected(thai.error())};
      }
      topics.push_back(Topic{.id = *id, .label_en = *english, .label_th = *thai});
    }
    return Result<void>{};
  });
  if (!parsed) {
    return std::unexpected(parsed.error());
  }
  std::ranges::sort(topics, {}, &Topic::id);
  return topics;
}

/// Every kind the definition tables accept, in the order the enum declares them: a type that names another
/// kind is a file that would be refused by the database, so it is refused here first and by file name.
constexpr auto FIELD_KINDS = std::array{std::string_view{"text"},    std::string_view{"integer"},
                                        std::string_view{"boolean"}, std::string_view{"choice"},
                                        std::string_view{"collection"}, std::string_view{"file"}};

auto string_list(const simdjson::dom::object& entry, std::string_view key) -> std::vector<std::string> {
  auto values = std::vector<std::string>{};
  auto array = simdjson::dom::array{};
  if (entry.at_key(key).get_array().get(array) != simdjson::SUCCESS) {
    return values;
  }
  for (const auto element : array) {
    auto text = std::string_view{};
    if (element.get_string().get(text) == simdjson::SUCCESS) {
      values.emplace_back(text);
    }
  }
  return values;
}

auto load_task_types(const std::filesystem::path& path) -> Result<std::vector<TaskTypeDefinition>> {
  auto types = std::vector<TaskTypeDefinition>{};
  const auto parsed = with_json(path, [&](const simdjson::dom::object& object) {
    auto array = simdjson::dom::array{};
    if (object.at_key("task_types").get_array().get(array) != simdjson::SUCCESS) {
      return Result<void>{std::unexpected(
          Error::make(ErrorCode::invalid_argument, path.string() + ": task_types is missing"))};
    }
    for (const auto element : array) {
      auto entry = simdjson::dom::object{};
      if (element.get_object().get(entry) != simdjson::SUCCESS) {
        return Result<void>{std::unexpected(Error::make(
            ErrorCode::invalid_argument, path.string() + ": a task type is not an object"))};
      }
      const auto id = required_text(entry, "id", path.string());
      if (!id) {
        return Result<void>{std::unexpected(id.error())};
      }
      auto type = TaskTypeDefinition{.id = *id,
                                     .title_en = {},
                                     .title_th = {},
                                     .position = 0,
                                     .fields = {}};
      type.title_en = required_text(entry, "title_en", path.string()).value_or("");
      type.title_th = required_text(entry, "title_th", path.string()).value_or("");
      std::int64_t position = 0;
      if (entry.at_key("position").get_int64().get(position) != simdjson::SUCCESS) {
        position = 0;
      }
      type.position = static_cast<std::int32_t>(position);

      auto fields = simdjson::dom::array{};
      if (entry.at_key("fields").get_array().get(fields) != simdjson::SUCCESS) {
        return Result<void>{std::unexpected(Error::make(
            ErrorCode::invalid_argument, path.string() + ": " + *id + " has no fields"))};
      }
      for (const auto field_element : fields) {
        auto field = simdjson::dom::object{};
        if (field_element.get_object().get(field) != simdjson::SUCCESS) {
          return Result<void>{std::unexpected(Error::make(
              ErrorCode::invalid_argument, path.string() + ": a field is not an object"))};
        }
        auto definition = FieldDefinition{};
        const auto key = required_text(field, "key", path.string());
        if (!key) {
          return Result<void>{std::unexpected(key.error())};
        }
        definition.key = *key;
        const auto kind = required_text(field, "kind", path.string());
        if (!kind) {
          return Result<void>{std::unexpected(kind.error())};
        }
        const auto known = std::ranges::any_of(FIELD_KINDS, [&kind](std::string_view candidate) {
          return candidate == *kind;
        });
        if (!known) {
          return Result<void>{std::unexpected(Error::make(
              ErrorCode::invalid_argument,
              path.string() + ": " + *id + "." + *key + " names the kind " + *kind +
                  ", which is not one of the six"))};
        }
        definition.kind = *kind;
        definition.label_en = required_text(field, "label_en", path.string()).value_or(*key);
        definition.label_th = required_text(field, "label_th", path.string()).value_or("");
        bool is_required = false;
        if (field.at_key("required").get_bool().get(is_required) != simdjson::SUCCESS) {
          is_required = false;
        }
        definition.is_required = is_required;
        std::int64_t number = 0;
        if (field.at_key("minimum").get_int64().get(number) == simdjson::SUCCESS) {
          definition.minimum = static_cast<std::int32_t>(number);
        }
        if (field.at_key("maximum").get_int64().get(number) == simdjson::SUCCESS) {
          definition.maximum = static_cast<std::int32_t>(number);
        }
        if (field.at_key("position").get_int64().get(number) != simdjson::SUCCESS) {
          number = 0;
        }
        definition.position = static_cast<std::int32_t>(number);
        definition.choices = string_list(field, "choices");
        definition.members = string_list(field, "members");
        type.fields.push_back(std::move(definition));
      }
      types.push_back(std::move(type));
    }
    return Result<void>{};
  });
  if (!parsed) {
    return std::unexpected(parsed.error());
  }
  return types;
}

auto task_directories(const std::filesystem::path& content_root) -> std::vector<std::filesystem::path> {
  auto directories = std::vector<std::filesystem::path>{};
  const auto tasks = content_root / TASKS_DIR;
  if (!std::filesystem::is_directory(tasks)) {
    return directories;
  }
  for (const auto& entry : std::filesystem::directory_iterator{tasks}) {
    if (entry.is_directory()) {
      directories.push_back(entry.path());
    }
  }
  std::ranges::sort(directories);
  return directories;
}

/// Every topic a task names must exist in the vocabulary. The schema cannot say this — it is one table
/// pointing at another that the seeder writes — so the check lives where both are in hand.
auto check_vocabulary(const std::vector<Topic>& topics, const std::vector<Task>& tasks)
    -> Result<void> {
  for (const auto& task : tasks) {
    for (const auto& topic : task.topics) {
      const auto found = std::ranges::find(topics, topic, &Topic::id);
      if (found == topics.end()) {
        return std::unexpected(Error::make(
            ErrorCode::invalid_argument,
            task.id + ": topic \"" + topic + "\" is not in topics.json"));
      }
    }
  }
  return {};
}

auto digest_bank(const std::vector<Topic>& topics, std::vector<Task>& tasks,
                 const std::vector<Track>& tracks) -> std::string {
  std::ranges::sort(tasks, [](const Task& left, const Task& right) {
    return left.id == right.id ? left.version < right.version : left.id < right.id;
  });
  auto hasher = Hasher{};
  for (const auto& topic : topics) {
    hasher.add_field(topic.id);
    hasher.add_field(topic.label_en);
    hasher.add_field(topic.label_th);
  }
  for (const auto& task : tasks) {
    hasher.add_field(task.id);
    hasher.add_field(std::to_string(task.version));
    hasher.add_field(task.content_hash);
  }
  for (const auto& track : tracks) {
    hasher.add_field(track.id);
    hasher.add_field(std::to_string(track.position));
    hasher.add_field(track.content_hash);
  }
  return hasher.digest();
}

}  // namespace

auto load(const std::filesystem::path& content_root) -> Result<Bank> {
  auto topics = load_topics(content_root / TOPICS_FILE);
  if (!topics) {
    return std::unexpected(topics.error());
  }

  auto tasks = std::vector<Task>{};
  for (const auto& directory : task_directories(content_root)) {
    auto task = load_task(directory);
    if (!task) {
      return std::unexpected(task.error());
    }
    tasks.push_back(std::move(*task));
  }
  if (tasks.empty()) {
    return std::unexpected(
        Error::make(ErrorCode::invalid_argument, content_root.string() + ": the bank holds no task"));
  }

  const auto vocabulary = check_vocabulary(*topics, tasks);
  if (!vocabulary) {
    return std::unexpected(vocabulary.error());
  }

  auto tracks = load_tracks(content_root / TRACKS_FILE, *topics);
  if (!tracks) {
    return std::unexpected(tracks.error());
  }

  auto levels = load_levels(content_root / LEVELS_FILE);
  if (!levels) {
    return std::unexpected(levels.error());
  }

  auto task_types = load_task_types(content_root / TASK_TYPES_FILE);
  if (!task_types) {
    return std::unexpected(task_types.error());
  }

  auto bank = Bank{.topics = std::move(*topics), .tasks = std::move(tasks),
                   .tracks = std::move(*tracks), .levels = std::move(*levels),
                   .task_types = std::move(*task_types), .content_hash = {}};
  bank.content_hash = digest_bank(bank.topics, bank.tasks, bank.tracks);
  return bank;
}

}  // namespace cms::bank
