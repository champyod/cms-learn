#include <cms/api/task_view.hpp>

#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <sqlpp23/sqlpp23.h>

#include <cms/api/settings_route.hpp>
#include <cms/db/tables.hpp>

namespace cms::api {
namespace {

constexpr std::string_view PUBLISHED = "published";
constexpr std::string_view ENGLISH = "en";
constexpr std::string_view THAI = "th";

/// One visible task row, with the version its statements and examples are keyed by.
struct LocatedTask {
  TaskPage page;
  std::int32_t version{0};
};

/// The lookup is the same predicate the bank's next-task query uses: published, and visible to one of the
/// audience's principals. A task with no matching row is not served, so "hidden" and "absent" are one answer.
auto first_visible_task(db::connection& database, std::string_view task_id,
                        const std::vector<std::int64_t>& principals)
    -> std::optional<LocatedTask> {
  const auto is_visible = sqlpp::exists(
      sqlpp::select(db::taskVisibility.taskId)
          .from(db::taskVisibility)
          .where(db::taskVisibility.taskId == db::tasks.id &&
                 db::taskVisibility.principalId.in(principals)));

  for (const auto& row :
       database(sqlpp::select(db::tasks.id, db::tasks.version, db::tasks.titleEn, db::tasks.titleTh,
                              db::tasks.timeLimitMs, db::tasks.memoryLimitMb)
                    .from(db::tasks)
                    .where(db::tasks.id == std::string{task_id} &&
                           db::tasks.status == std::string{PUBLISHED} && is_visible))) {
    auto page = TaskPage{};
    page.id = std::string{row.id};
    page.title_en = std::string{row.titleEn};
    page.title_th = std::string{row.titleTh};
    page.time_limit_ms = static_cast<std::int32_t>(row.timeLimitMs);
    page.memory_limit_mb = static_cast<std::int32_t>(row.memoryLimitMb);
    return LocatedTask{.page = std::move(page),
                       .version = static_cast<std::int32_t>(row.version)};
  }
  return std::nullopt;
}

auto read_statements(db::connection& database, const LocatedTask& task, TaskPage& page) -> void {
  for (const auto& row :
       database(sqlpp::select(db::taskStatements.locale, db::taskStatements.body)
                    .from(db::taskStatements)
                    .where(db::taskStatements.taskId == task.page.id &&
                           db::taskStatements.taskVersion ==
                               static_cast<std::int64_t>(task.version)))) {
    const auto locale = std::string{row.locale};
    if (locale == ENGLISH) {
      page.statement_en = std::string{row.body};
    } else if (locale == THAI) {
      page.statement_th = std::string{row.body};
    }
  }
}

/// Only the first example is rendered: one worked input and its expected output is what teaches the format,
/// and the page has one pair of slots for it.
auto read_first_example(db::connection& database, const LocatedTask& task, TaskPage& page) -> bool {
  for (const auto& row : database(sqlpp::select(db::taskExamples.input,
                                                db::taskExamples.expectedOutput)
                                      .from(db::taskExamples)
                                      .where(db::taskExamples.taskId == task.page.id &&
                                             db::taskExamples.taskVersion ==
                                                 static_cast<std::int64_t>(task.version))
                                      .order_by(db::taskExamples.ordinal.asc())
                                      .limit(1))) {
    page.example_input = std::string{row.input};
    page.example_output = std::string{row.expectedOutput};
    return true;
  }
  return false;
}

}  // namespace

auto audience_of_session(db::connection& database, std::string_view session_id)
    -> Result<bank::Audience> {
  const auto principal = principal_of_session(database, session_id);
  if (!principal) {
    return std::unexpected(principal.error());
  }
  if (principal->empty()) {
    return bank::Audience{bank::everyone()};
  }
  return bank::request_audience(database, *principal);
}

auto read_task_page(db::connection& database, std::string_view task_id,
                    std::span<const std::int64_t> audience) -> Result<std::optional<TaskPage>> {
  if (audience.empty()) {
    return std::unexpected(Error::make(
        ErrorCode::invalid_argument, "the audience is empty; every request carries the everyone entry"));
  }
  try {
    const auto principals = std::vector<std::int64_t>(audience.begin(), audience.end());
    auto located = first_visible_task(database, task_id, principals);
    if (!located) {
      return std::optional<TaskPage>{};
    }
    read_statements(database, *located, located->page);
    if (!read_first_example(database, *located, located->page)) {
      return std::unexpected(Error::make(
          ErrorCode::internal,
          "the published task " + std::string{task_id} + " has no example to render"));
    }
    if (located->page.statement_en.empty() && located->page.statement_th.empty()) {
      return std::unexpected(Error::make(
          ErrorCode::internal,
          "the published task " + std::string{task_id} + " has no statement to render"));
    }
    return std::optional<TaskPage>{std::move(located->page)};
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }
}

}  // namespace cms::api
