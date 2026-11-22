#pragma once

#include <memory>
#include <string>

#include <cms/db/connection.hpp>

namespace cms::api {

/// @brief Registers the one-shot verdict fragment and the stream that pushes updates to the task page.
///
/// @param database_config The pool the session, the task and its submissions are read through.
/// @param database_url The libpq connection string the stream's listener connects with.
///
/// Both routes require the same approved session as the page and answer the page's one 404 when the task is
/// not servable; the stream additionally ends on its own timeout and is capped per session.
auto register_verdict_route(
    const std::shared_ptr<const db::connection_config>& database_config,
    const std::string& database_url) -> void;

}  // namespace cms::api
