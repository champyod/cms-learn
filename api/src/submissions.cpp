#include <cms/api/submissions.hpp>

#include <chrono>
#include <exception>
#include <string>
#include <string_view>

#include <sqlpp23/sqlpp23.h>

#include <cms/db/tables.hpp>

namespace cms::api {
namespace {

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
    const auto account = account_of(database, session_id);
    if (account.empty()) {
      return std::unexpected(
          Error::make(ErrorCode::permission_denied, "no live session; sign in before submitting"));
    }
    const auto version = published_version(database, task_id);
    if (version == 0) {
      return std::unexpected(
          Error::make(ErrorCode::not_found, "no published task called " + std::string{task_id}));
    }

    database(sqlpp::insert_into(db::submissions).set(
        db::submissions.accountId = account, db::submissions.taskId = std::string{task_id},
        db::submissions.taskVersion = version, db::submissions.source = std::string{source}));
    return Submission{.id = {}, .task_id = std::string{task_id}};
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::conflict, error.what()));
  }
}

}  // namespace cms::api
