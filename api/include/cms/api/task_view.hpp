#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <cms/api/web.hpp>
#include <cms/bank/visibility.hpp>
#include <cms/db/connection.hpp>
#include <cms/error.hpp>

namespace cms::api {

/// @brief The principal a request runs as, and the audience its visibility is decided with.
struct TaskViewer {
  std::string principal_id;
  bank::Audience audience;
};

/// @brief Resolves a session cookie to an approved principal and its audience.
///
/// @param database The connection to read through.
/// @param session_id The opaque cookie value; empty when the request carried none.
/// @return The viewer, or a permission_denied error for an absent, expired or revoked session, or for an
/// account that is not active. A request without a live approved session has no audience at all.
///
/// The encoding belongs to `bank::visibility`; this function only decides which principal the cookie names.
[[nodiscard]] Result<TaskViewer> task_viewer(db::connection& database,
                                            std::string_view session_id);

/// @brief The version the bank serves for @p task_id: the highest `published` one.
///
/// @return The version, nothing when no version of the task is published, or the reason the read failed.
///
/// One definition so the page and the submission write cannot pick different versions of the same task.
[[nodiscard]] Result<std::optional<std::int32_t>> published_version(db::connection& database,
                                                                    std::string_view task_id);

/// @brief The published version of @p task_id the request may see.
///
/// @param database The connection to read through.
/// @param task_id The id the route asked for.
/// @param audience The encoded principals this request runs with.
/// @param may_read_hidden True when the viewer holds `content:read`, the capability that reveals a task with
/// no visibility rows.
/// @return The servable version, nothing when the task is unknown, not published, or has no visibility row
/// the audience matches, or the reason the read failed.
[[nodiscard]] Result<std::optional<std::int32_t>> servable_version(
    db::connection& database, std::string_view task_id, std::span<const std::int64_t> audience,
    bool may_read_hidden);

/// @brief Reads the page data for the servable version of @p task_id.
///
/// @return The page data, nothing under the same conditions as `servable_version`, or the reason the read
/// failed. Unknown, draft and hidden are one answer on purpose: the route must not distinguish them.
[[nodiscard]] Result<std::optional<TaskPage>> read_task_page(
    db::connection& database, std::string_view task_id, std::span<const std::int64_t> audience,
    bool may_read_hidden);

}  // namespace cms::api
