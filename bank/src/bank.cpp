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

namespace cms::bank {
namespace {

using detail::Hasher;
using detail::required_text;
using detail::with_json;

constexpr std::string_view TOPICS_FILE = "topics.json";
constexpr std::string_view TASKS_DIR = "tasks";

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

auto digest_bank(const std::vector<Topic>& topics, std::vector<Task>& tasks) -> std::string {
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

  auto bank = Bank{.topics = std::move(*topics), .tasks = std::move(tasks), .content_hash = {}};
  bank.content_hash = digest_bank(bank.topics, bank.tasks);
  return bank;
}

}  // namespace cms::bank
