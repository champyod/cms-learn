#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <cms/api/web.hpp>
#include <cms/bank/bank.hpp>
#include <cms/bank/visibility.hpp>
#include <cms/db/connection.hpp>
#include <cms/error.hpp>

namespace cms::api {

/// @brief The account a request runs as, and the audience its visibility is decided with.
struct TaskViewer {
  /// The spelling `sessions.account_kind` stores: "learner", "admin" or "system".
  std::string account_kind;
  std::string account_id;
  bank::Audience audience;
};

/// @brief Resolves a session cookie to an approved account and its audience.
///
/// @param database The connection to read through.
/// @param session_id The opaque cookie value; empty when the request carried none.
/// @return The viewer, or a permission_denied error for an absent, expired or revoked session, or
/// for an account that is not active. A request without a live approved session has no audience.
///
/// The encoding belongs to `bank::visibility`; this function only decides which account the cookie
/// names.
[[nodiscard]] Result<TaskViewer> task_viewer(db::connection& database,
                                            std::string_view session_id);

/// @brief The version the bank serves for @p task_id: the highest `published` one.
///
/// @return The version, nothing when no version of the task is published, or the reason the read
/// failed.
///
/// One definition so the page and the submission write cannot pick different versions of the same
/// task.
[[nodiscard]] Result<std::optional<std::int32_t>> published_version(db::connection& database,
                                                                    std::string_view task_id);

/// @brief The published version of @p task_id the request may see.
///
/// @param database The connection to read through.
/// @param task_id The id the route asked for.
/// @param audience The encoded accounts and teams this request runs with.
/// @param may_read_hidden True when the viewer holds `content:read`, the capability that reveals a
/// task with no visibility rows.
/// @return The servable version, nothing when the task is unknown, not published, or has no
/// visibility row the audience matches, or the reason the read failed.
[[nodiscard]] Result<std::optional<std::int32_t>> servable_version(
    db::connection& database, std::string_view task_id, std::span<const std::int64_t> audience,
    bool may_read_hidden);

/// @brief The narrowing a list request asked for, every part already validated by the route.
///
/// The two bands are kept apart rather than intersected here: a level band is the learner's choice and a
/// stage band is the route's, and a reader of the query can see both without reconstructing which was which.
struct TaskListFilters {
  std::optional<std::int32_t> level_min;
  std::optional<std::int32_t> level_max;
  std::optional<std::int32_t> stage_min;
  std::optional<std::int32_t> stage_max;
  /// Vocabulary ids the task must carry at least one of; empty means no narrowing by topic.
  std::vector<std::string> topics;
  /// The applied stage's tags, the same rule as `topics`.
  std::vector<std::string> stage_topics;
};

/// @brief The published, visible tasks the filters leave, easiest first.
///
/// @param database The connection to read through.
/// @param filters The narrowing the request asked for.
/// @param audience The encoded accounts this request runs with.
/// @param may_read_hidden True when the viewer holds `content:read`, which reveals a task with no
/// visibility rows.
/// @return One row per task id — its highest published version — ordered by difficulty and id, or the
/// reason the read failed.
///
/// A task with no visibility row is absent for a learner and present for a reader, the same rule the task
/// page applies, so the list cannot show a link the page would then refuse.
[[nodiscard]] Result<std::vector<TaskListItem>> read_task_list(
    db::connection& database, const TaskListFilters& filters,
    std::span<const std::int64_t> audience, bool may_read_hidden);

/// @brief Every topic the vocabulary defines, ordered by id, as the filter offers them.
[[nodiscard]] Result<std::vector<bank::Topic>> read_topic_vocabulary(db::connection& database);

/// @brief Reads the page data for the servable version of @p task_id.
///
/// @return The page data, nothing under the same conditions as `servable_version`, or the reason
/// the read failed. Unknown, draft and hidden are one answer on purpose: the route must not
/// distinguish them.
[[nodiscard]] Result<std::optional<TaskPage>> read_task_page(
    db::connection& database, std::string_view task_id, std::span<const std::int64_t> audience,
    bool may_read_hidden);

}  // namespace cms::api
