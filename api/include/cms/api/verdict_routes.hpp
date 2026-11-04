#pragma once

#include <memory>

#include <cms/db/connection.hpp>

namespace cms::api {

/// @brief Registers `GET /tasks/{id}/verdict`, the fragment the task page polls.
///
/// @param database_config The pool the session, the task and its submissions are read through.
///
/// The route requires the same approved session as the page, answers the page's one 404 when the task is not
/// servable, and returns the fragment `task.html` swaps into `#verdict`.
auto register_verdict_route(
    const std::shared_ptr<const db::connection_config>& database_config) -> void;

}  // namespace cms::api
