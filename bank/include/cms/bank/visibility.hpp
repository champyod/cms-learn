#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include <cms/db/connection.hpp>
#include <cms/error.hpp>

namespace cms::bank {

/// @brief What an encoded audience value is for; the kind is carried in the number itself.
enum class AudienceKind : std::uint8_t { everyone, learner, administrator, team };

/// @brief One decoded `task_visibility.principal_id`.
struct Principal {
  AudienceKind kind{AudienceKind::everyone};
  /// Zero for the everyone entry; otherwise the identifier of the learner, administrator or team.
  std::int64_t id{0};
};

/// @brief The encoded values one serving request runs with: everyone, the viewer, and their teams.
using Audience = std::vector<std::int64_t>;

/// @brief Who created a task, which decides the visibility rows it is born with.
enum class AuthorKind : std::uint8_t { learner, administrator };

/// @brief The encoded value that grants a task to everyone.
[[nodiscard]] auto everyone() -> std::int64_t;

/// @brief The largest identifier one encoded kind can carry; an id above it is refused.
[[nodiscard]] auto identifier_limit() -> std::int64_t;

/// @brief Encodes @p id under @p kind into the one integer `task_visibility` stores.
/// @return The encoded value, or an error naming the kind and id when the pair cannot be encoded.
[[nodiscard]] auto encode(AudienceKind kind, std::int64_t id) -> Result<std::int64_t>;

/// @brief Decodes one stored `task_visibility.principal_id`.
/// @return The kind and id, or an error when the value is outside the four legal ranges.
[[nodiscard]] auto decode(std::int64_t value) -> Result<Principal>;

/// @brief The audience a serving request runs with: everyone, the viewer, and the viewer's teams.
/// @return The encoded values, or an error naming the id that cannot be encoded.
[[nodiscard]] auto audience_for(std::int64_t viewer_id, std::span<const std::int64_t> team_ids)
    -> Result<Audience>;

/// @brief The numeric id `principals.serial_id` assigns to one principal.
/// @param database The connection to read through.
/// @param principal_id The session principal's uuid.
/// @return The serial id, or an error when no principal row carries that uuid.
[[nodiscard]] auto serial_id_of(db::connection& database, std::string_view principal_id)
    -> Result<std::int64_t>;

/// @brief The audience one session principal's requests run with: everyone, then the principal.
/// @return The encoded audience, or an error naming the uuid when no principal row exists.
///
/// Teams are not modelled yet, so only the viewer's own value is composed; the everyone entry keeps a
/// task granted to 0 reachable.
[[nodiscard]] auto request_audience(db::connection& database, std::string_view principal_id)
    -> Result<Audience>;

/// @brief The visibility rows a new task carries, by who created it.
/// @return One everyone row for a learner-created task; no rows for an administrator-created one.
[[nodiscard]] auto default_visibility(AuthorKind author) -> Audience;

}  // namespace cms::bank
