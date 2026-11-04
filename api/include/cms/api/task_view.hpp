#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include <cms/api/web.hpp>
#include <cms/bank/visibility.hpp>
#include <cms/db/connection.hpp>
#include <cms/error.hpp>

namespace cms::api {

/// @brief The audience a request runs with, read from its session cookie.
///
/// @param database The connection to read through.
/// @param session_id The opaque cookie value; empty when the request carried none.
/// @return `{everyone()}` when no live session names a principal, otherwise the everyone entry plus the
/// viewer's own encoded value. An absent or dead session is not an error: it still sees shared tasks.
///
/// The encoding belongs to `bank::visibility`; this function only decides which principal the cookie names.
[[nodiscard]] Result<bank::Audience> audience_of_session(db::connection& database,
                                                         std::string_view session_id);

/// @brief Reads the published task @p task_id when @p audience may see it.
///
/// @param database The connection to read through.
/// @param task_id The id the route asked for.
/// @param audience The encoded principals this request runs with, always starting with the everyone entry.
/// @return The page data, nothing when the task is unknown, not `published`, or has no visibility row the
/// audience matches, or the reason a read failed.
///
/// Unknown, draft and hidden are one answer on purpose: the route must not distinguish them, so it cannot
/// be used to enumerate hidden ids.
[[nodiscard]] Result<std::optional<TaskPage>> read_task_page(
    db::connection& database, std::string_view task_id,
    std::span<const std::int64_t> audience);

}  // namespace cms::api
