#pragma once

#include <string>
#include <string_view>

#include <cms/db/connection.hpp>
#include <cms/error.hpp>

namespace cms::api {

/// @brief What a submission produced.
struct Submission {
  std::string id;
  std::string task_id;
};

/// @brief Records what an account submitted for one task.
///
/// @param database The connection; the caller owns the transaction.
/// @param session_id The opaque cookie value, which is the session's primary key.
/// @param task_id The task the submission is for.
/// @param source The submitted program.
/// @return The new submission, or the reason it was refused.
///
/// The account is derived from the session, never from the body: a client that could name its own account
/// could submit as anyone. A session that is absent, expired or revoked is refused here rather than by a
/// check the handler might forget.
[[nodiscard]] Result<Submission> submit_solution(db::connection& database, std::string_view session_id,
                                                 std::string_view task_id,
                                                 std::string_view source);

}  // namespace cms::api
