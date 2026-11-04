#include <cms/api/submissions.hpp>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <cms/api/task_view.hpp>
#include <cms/api/uuid.hpp>
#include <cms/db/tables.hpp>

namespace cms::api {
namespace {

/// The statement's fixed parameters come first, so the audience's placeholders start after them.
constexpr std::size_t FIRST_AUDIENCE_PARAMETER = 6;

/// One statement writes the row and re-checks what the row depends on at the moment it runs.
///
/// A lookup followed by an insert leaves a window between the two: a session can be revoked, a task
/// unpublished or its audience narrowed after the read that authorised the write. Here the row exists only
/// if every condition holds inside the statement that inserts it, so there is no window at all. The version
/// is bound rather than read from the joined row, so it is the same version the page renders; a publish
/// between the read and this write changes no row and the client retries.
constexpr std::string_view SUBMIT_HEAD = R"(
INSERT INTO submissions (account_id, task_id, task_version, source)
SELECT sessions.principal_id, tasks.id, tasks.version, $1
FROM sessions
CROSS JOIN tasks
WHERE sessions.id = $2
  AND sessions.revoked_at IS NULL
  AND sessions.expires_at > $3
  AND tasks.id = $4
  AND tasks.version = $5
  AND tasks.status = 'published'
  AND EXISTS (SELECT 1 FROM task_visibility v
              WHERE v.task_id = tasks.id
                AND v.principal_id IN ()";
constexpr std::string_view SUBMIT_TAIL = "))";

/// The audience is a list whose length is only known at run time, so its placeholders are counted here.
/// The values still travel as bound parameters; nothing a caller supplied is part of the statement text.
auto audience_placeholders(std::size_t count) -> std::string {
  auto text = std::string{};
  for (auto index = std::size_t{0}; index < count; ++index) {
    if (index != 0) {
      text += ", ";
    }
    text += "$" + std::to_string(FIRST_AUDIENCE_PARAMETER + index);
  }
  return text;
}

auto submit_sql(std::size_t audience_size) -> std::string {
  auto sql = std::string{SUBMIT_HEAD};
  sql += audience_placeholders(audience_size);
  sql += SUBMIT_TAIL;
  return sql;
}

/// The session is the cookie. A row that has expired or been revoked is not a session, and the query says so
/// rather than leaving the caller to remember.
auto account_of(db::connection& database, std::string_view session_id) -> std::string {
  auto account = std::string{};
  for (const auto& row : database(sqlpp::select(db::sessions.principalId)
                                      .from(db::sessions)
                                      .where(db::sessions.id == std::string{session_id} &&
                                             db::sessions.revokedAt.is_null() &&
                                             db::sessions.expiresAt > db::now_stamp()))) {
    account = std::string{row.principalId};
  }
  return account;
}

}  // namespace

auto submit_solution(db::connection& database, std::string_view session_id, std::string_view task_id,
                     std::string_view source, std::span<const std::int64_t> audience)
    -> Result<Submission> {
  if (source.empty()) {
    return std::unexpected(
        Error::make(ErrorCode::invalid_argument, "a submission needs a program"));
  }
  if (audience.empty()) {
    return std::unexpected(Error::make(ErrorCode::invalid_argument,
                                       "the audience is empty; a request carries its viewer"));
  }
  // The cookie is client input. An absent one arrives here as an empty string, and anything that is not a
  // uuid cannot name a session, so both are refused here rather than cast against a uuid column: that raises
  // a type error, which surfaces the server's message instead of a reason a student can act on.
  if (!has_uuid_shape(session_id)) {
    return std::unexpected(
        Error::make(ErrorCode::permission_denied, "no live session; sign in before submitting"));
  }
  const auto version = published_version(database, task_id);
  if (!version) {
    return std::unexpected(version.error());
  }
  if (!*version) {
    return std::unexpected(
        Error::make(ErrorCode::not_found, "no published task called " + std::string{task_id}));
  }
  try {
    const auto principals = std::vector<std::int64_t>(audience.begin(), audience.end());
    auto parameters = std::vector<db::bound_value>{std::string{source}, std::string{session_id},
                                                   db::now_stamp(), std::string{task_id},
                                                   static_cast<std::int64_t>(**version)};
    for (const auto principal : principals) {
      parameters.emplace_back(principal);
    }
    const auto written = database.execute_text(submit_sql(principals.size()), std::move(parameters));
    if (written == 1) {
      return Submission{.id = {}, .task_id = std::string{task_id}};
    }
    // Nothing was written, so this is a refusal. The two the caller can cause are told apart only here,
    // where the extra read costs nothing and the message stays the one the route already returns.
    if (account_of(database, session_id).empty()) {
      return std::unexpected(
          Error::make(ErrorCode::permission_denied, "no live session; sign in before submitting"));
    }
    const auto current = published_version(database, task_id);
    if (!current || !*current) {
      return std::unexpected(
          Error::make(ErrorCode::not_found, "no published task called " + std::string{task_id}));
    }
    return std::unexpected(Error::make(ErrorCode::conflict, "the submission was refused"));
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::conflict, error.what()));
  }
}

}  // namespace cms::api
