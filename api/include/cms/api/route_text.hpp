#pragma once

#include <string_view>

namespace cms::api {

/// @brief The one body an unknown, draft, hidden or otherwise unservable task answers.
///
/// A caller cannot tell which of those it hit, so the route is not an oracle for the ids of tasks it may not
/// see. The page and the verdict fragment share it so the two cannot drift.
inline constexpr std::string_view NO_TASK_BODY = "no such task\n";

/// @brief The one 500 body a page route returns.
///
/// It names neither the principal nor the task: a detail in a response is an enumeration oracle, and the
/// service has no per-request channel to put it on instead.
inline constexpr std::string_view INTERNAL_ERROR_BODY = "the request could not be served\n";

}  // namespace cms::api
