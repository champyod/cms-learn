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
constexpr std::string_view ACTIVE_STATUS = "active";
constexpr std::string_view ENGLISH = "en";
constexpr std::string_view THAI = "th";

auto task_row_from(const auto& row) -> TaskPage {
  auto page = TaskPage{};
  page.id = std::string{row.id};
  page.title_en = std::string{row.titleEn};
  page.title_th = std::string{row.titleTh};
  page.time_limit_ms = static_cast<std::int32_t>(row.timeLimitMs);
  page.memory_limit_mb = static_cast<std::int32_t>(row.memoryLimitMb);
  return page;
}

/// Published, and visible to one of the audience's principals: the same predicate the bank's next-task query
/// uses, so "hidden" and "absent" are one answer.
auto visible_task_row(db::connection& database, std::string_view task_id, std::int32_t version,
                      const std::vector<std::int64_t>& principals) -> std::optional<TaskPage> {
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
                           db::tasks.version == static_cast<std::int64_t>(version) &&
                           db::tasks.status == std::string{PUBLISHED} && is_visible))) {
    return task_row_from(row);
  }
  return std::nullopt;
}

/// The row a viewer holding `content:read` sees: the published version, whatever its visibility rows say.
auto any_task_row(db::connection& database, std::string_view task_id, std::int32_t version)
    -> std::optional<TaskPage> {
  for (const auto& row :
       database(sqlpp::select(db::tasks.id, db::tasks.version, db::tasks.titleEn, db::tasks.titleTh,
                              db::tasks.timeLimitMs, db::tasks.memoryLimitMb)
                    .from(db::tasks)
                    .where(db::tasks.id == std::string{task_id} &&
                           db::tasks.version == static_cast<std::int64_t>(version) &&
                           db::tasks.status == std::string{PUBLISHED}))) {
    return task_row_from(row);
  }
  return std::nullopt;
}

auto read_statements(db::connection& database, std::string_view task_id, std::int32_t version,
                     TaskPage& page) -> void {
  for (const auto& row :
       database(sqlpp::select(db::taskStatements.locale, db::taskStatements.body)
                    .from(db::taskStatements)
                    .where(db::taskStatements.taskId == std::string{task_id} &&
                           db::taskStatements.taskVersion == static_cast<std::int64_t>(version)))) {
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
auto read_first_example(db::connection& database, std::string_view task_id, std::int32_t version,
                        TaskPage& page) -> bool {
  for (const auto& row : database(sqlpp::select(db::taskExamples.input,
                                                db::taskExamples.expectedOutput)
                                      .from(db::taskExamples)
                                      .where(db::taskExamples.taskId == std::string{task_id} &&
                                             db::taskExamples.taskVersion ==
                                                 static_cast<std::int64_t>(version))
                                      .order_by(db::taskExamples.ordinal.asc())
                                      .limit(1))) {
    page.example_input = std::string{row.input};
    page.example_output = std::string{row.expectedOutput};
    return true;
  }
  return false;
}

}  // namespace

auto task_viewer(db::connection& database, std::string_view session_id) -> Result<TaskViewer> {
  const auto principal = principal_of_session(database, session_id);
  if (!principal) {
    return std::unexpected(principal.error());
  }
  if (principal->empty()) {
    return std::unexpected(
        Error::make(ErrorCode::permission_denied, "no live session; sign in first"));
  }

  auto status = std::string{};
  try {
    for (const auto& row : database(sqlpp::select(db::principals.status)
                                        .from(db::principals)
                                        .where(db::principals.id == *principal))) {
      status = std::string{row.status};
    }
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }
  if (status.empty() || status != ACTIVE_STATUS) {
    return std::unexpected(Error::make(ErrorCode::permission_denied,
                                       "this account is not approved; it cannot view a task"));
  }

  const auto audience = bank::request_audience(database, *principal);
  if (!audience) {
    return std::unexpected(audience.error());
  }
  return TaskViewer{.principal_id = *principal, .audience = *audience};
}

auto published_version(db::connection& database, std::string_view task_id)
    -> Result<std::optional<std::int32_t>> {
  try {
    for (const auto& row :
         database(sqlpp::select(db::tasks.version)
                      .from(db::tasks)
                      .where(db::tasks.id == std::string{task_id} &&
                             db::tasks.status == std::string{PUBLISHED})
                      .order_by(db::tasks.version.desc())
                      .limit(1))) {
      return static_cast<std::int32_t>(row.version);
    }
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }
  return std::optional<std::int32_t>{};
}

auto servable_version(db::connection& database, std::string_view task_id,
                      std::span<const std::int64_t> audience, bool may_read_hidden)
    -> Result<std::optional<std::int32_t>> {
  if (audience.empty()) {
    return std::unexpected(
        Error::make(ErrorCode::invalid_argument, "the audience is empty; a request carries its viewer"));
  }
  const auto version = published_version(database, task_id);
  if (!version) {
    return std::unexpected(version.error());
  }
  if (!*version) {
    return std::optional<std::int32_t>{};
  }
  if (may_read_hidden) {
    return *version;
  }

  try {
    const auto principals = std::vector<std::int64_t>(audience.begin(), audience.end());
    for (const auto& row :
         database(sqlpp::select(db::taskVisibility.taskId)
                      .from(db::taskVisibility)
                      .where(db::taskVisibility.taskId == std::string{task_id} &&
                             db::taskVisibility.principalId.in(principals))
                      .limit(1))) {
      (void)row;
      return *version;
    }
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }
  return std::optional<std::int32_t>{};
}

auto read_task_page(db::connection& database, std::string_view task_id,
                    std::span<const std::int64_t> audience, bool may_read_hidden)
    -> Result<std::optional<TaskPage>> {
  const auto version = servable_version(database, task_id, audience, may_read_hidden);
  if (!version) {
    return std::unexpected(version.error());
  }
  if (!*version) {
    return std::optional<TaskPage>{};
  }

  try {
    const auto principals = std::vector<std::int64_t>(audience.begin(), audience.end());
    auto page = may_read_hidden ? any_task_row(database, task_id, **version)
                                : visible_task_row(database, task_id, **version, principals);
    if (!page) {
      return std::optional<TaskPage>{};
    }
    read_statements(database, task_id, **version, *page);
    if (!read_first_example(database, task_id, **version, *page)) {
      return std::unexpected(Error::make(
          ErrorCode::internal, "the servable task has no example to render"));
    }
    if (page->statement_en.empty() && page->statement_th.empty()) {
      return std::unexpected(
          Error::make(ErrorCode::internal, "the servable task has no statement to render"));
    }
    return page;
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }
}

}  // namespace cms::api
