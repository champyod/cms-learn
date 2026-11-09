#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace cms::api {

/// Who is acting: which account table the id names, and the id. A surface produces this; a request may
/// never carry it.
struct Actor {
  std::string kind;
  std::string id;
};

/// The account kinds every actor column admits, spelled once so no writer invents a fourth.
constexpr std::string_view ACTOR_KIND_LEARNER = "learner";
constexpr std::string_view ACTOR_KIND_ADMIN = "admin";
constexpr std::string_view ACTOR_KIND_SYSTEM = "system";

[[nodiscard]] constexpr auto is_actor_kind(std::string_view kind) -> bool {
  return kind == ACTOR_KIND_LEARNER || kind == ACTOR_KIND_ADMIN || kind == ACTOR_KIND_SYSTEM;
}

/// The id when @p actor is of @p kind, otherwise nothing: a per-kind column takes exactly one of these.
[[nodiscard]] inline auto actor_id_of(const Actor& actor, std::string_view kind)
    -> std::optional<std::string> {
  return actor.kind == kind ? std::optional<std::string>{actor.id} : std::nullopt;
}

}  // namespace cms::api
