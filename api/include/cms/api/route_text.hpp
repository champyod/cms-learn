#pragma once

#include <string_view>

namespace cms::api {

/// @brief The one body an unknown, draft, hidden or otherwise unservable task answers.
///
/// A caller cannot tell which of those it hit, so the route is not an oracle for the ids of tasks it may not
/// see. The page and the verdict fragment share it so the two cannot drift.
inline constexpr std::string_view NO_TASK_BODY = "no such task\n";

/// A row that exists and is published, and that no verified certificate covers: the reader who can see it is the one who
/// would publish it, so telling them "no such task" would tell them the opposite of the truth (ADR 0057 §3).
inline constexpr std::string_view UNCERTIFIED_TASK_BODY =
    "this task is published, and no verified certificate exists for this version, so it cannot be shown\n";

/// @brief The one 500 body a page route returns.
///
/// It names neither the principal nor the task: a detail in a response is an enumeration oracle, and the
/// service has no per-request channel to put it on instead.
inline constexpr std::string_view INTERNAL_ERROR_BODY = "the request could not be served\n";

}  // namespace cms::api
