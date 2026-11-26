#pragma once

#include <cstdint>
#include <cstdint>
#include <span>
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
/// @param audience The encoded principals this request runs with, always starting with the everyone entry.
/// @return The new submission, or the reason it was refused.
///
/// The account is derived from the session, never from the body: a client that could name its own account
/// could submit as anyone. A session that is absent, expired or revoked is refused here rather than by a
/// check the handler might forget. A task is writable only when it is published and a visibility row
/// matches @p audience, so a hidden or unpublished task cannot be submitted to.
[[nodiscard]] Result<Submission> submit_solution(db::connection& database, std::string_view session_id,
                                                 std::string_view task_id,
                                                 std::string_view source,
                                                 std::span<const std::int64_t> audience);

/// @brief The judge's backlog, as two numbers an operator can alert on.
struct JudgeQueue {
  /// Submissions waiting for a verdict.
  std::int64_t pending{0};
  /// How long the oldest of them has waited, in seconds; zero when nothing waits.
  std::int64_t oldest_waiting_seconds{0};
};

/// @brief Reads the backlog. One row of two aggregates, so a scrape costs one query.
///
/// @param config The pool every handler already holds, by the same smart pointer the routes pass around.
///               The pointer is the parameter rather than a reference because a handler holds this value
///               and a smart pointer does not convert, and because a null config is exactly the case a
///               scrape must survive: it is reported as unreadable, not dereferenced.
/// @return The backlog, or the reason it could not be read: a metrics endpoint that reports this queue
///         renders the gauges as unknown rather than failing the whole scrape.
[[nodiscard]] auto judge_queue(const std::shared_ptr<const db::connection_config>& config) -> Result<JudgeQueue>;

}  // namespace cms::api
