#include <cms/api/submissions.hpp>

#include <chrono>
#include <exception>
#include <string>
#include <string_view>
#include <vector>

#include <sqlpp23/sqlpp23.h>

#include <cms/db/tables.hpp>

namespace cms::api {
namespace {

/// One statement writes the row and re-checks what the row depends on at the moment it runs.
///
/// A lookup followed by an insert leaves a window between the two: a session can be revoked or a task
/// unpublished after the read that authorised the write. Here the row exists only if both conditions hold
/// inside the statement that inserts it, so there is no window at all.
constexpr std::string_view SUBMIT_SQL = R"(
INSERT INTO submissions (account_id, task_id, task_version, source)
SELECT sessions.principal_id, tasks.id, tasks.version, $1
FROM sessions
CROSS JOIN tasks
WHERE sessions.id = $2
  AND sessions.revoked_at IS NULL
  AND sessions.expires_at > $3
  AND tasks.id = $4
  AND tasks.status = 'published'
)";

auto now_stamp() -> cms::db::timestamp {
  return std::chrono::time_point_cast<cms::db::timestamp::duration>(
      std::chrono::system_clock::now());
}

/// The session is the cookie. A row that has expired or been revoked is not a session, and the query says so
/// rather than leaving the caller to remember.
auto account_of(db::connection& database, std::string_view session_id) -> std::string {
  auto account = std::string{};
  for (const auto& row : database(sqlpp::select(db::sessions.principalId)
                                      .from(db::sessions)
                                      .where(db::sessions.id == std::string{session_id} &&
                                             db::sessions.revokedAt.is_null() &&
                                             db::sessions.expiresAt > now_stamp()))) {
    account = std::string{row.principalId};
  }
  return account;
}

auto published_version(db::connection& database, std::string_view task_id) -> std::int32_t {
  auto version = 0;
  for (const auto& row : database(sqlpp::select(db::tasks.version)
                                      .from(db::tasks)
                                      .where(db::tasks.id == std::string{task_id} &&
                                             db::tasks.status == std::string{"published"}))) {
    version = static_cast<std::int32_t>(row.version);
  }
  return version;
}

}  // namespace

auto submit_solution(db::connection& database, std::string_view session_id, std::string_view task_id,
                     std::string_view source) -> Result<Submission> {
  if (source.empty()) {
    return std::unexpected(
        Error::make(ErrorCode::invalid_argument, "a submission needs a program"));
  }
  try {
    const auto written = database.execute_text(
        std::string{SUBMIT_SQL},
        {std::string{source}, std::string{session_id}, now_stamp(), std::string{task_id}});
    if (written == 1) {
      return Submission{.id = {}, .task_id = std::string{task_id}};
    }
    // Nothing was written, so this is a refusal. The two the caller can cause are told apart only here,
    // where the extra read costs nothing and the message stays the one the route already returns.
    if (account_of(database, session_id).empty()) {
      return std::unexpected(
          Error::make(ErrorCode::permission_denied, "no live session; sign in before submitting"));
    }
    if (published_version(database, task_id) == 0) {
      return std::unexpected(
          Error::make(ErrorCode::not_found, "no published task called " + std::string{task_id}));
    }
    return std::unexpected(Error::make(ErrorCode::conflict, "the submission was refused"));
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::conflict, error.what()));
  }
}

}  // namespace cms::api
