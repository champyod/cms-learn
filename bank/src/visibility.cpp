#include <cms/bank/visibility.hpp>

#include <string>
#include <string_view>

#include <sqlpp23/sqlpp23.h>

#include <cms/db/tables.hpp>

namespace cms::bank {
namespace {

// 2^48, the width of one encoded kind; the one place the number is stated in code.
constexpr std::int64_t SPAN = 281474976710656;
constexpr std::int64_t IDENTIFIER_LIMIT = SPAN - 1;
constexpr std::int64_t LEARNER_KIND = 1;
constexpr std::int64_t ADMINISTRATOR_KIND = 2;
constexpr std::int64_t TEAM_KIND = 3;

auto name(AudienceKind kind) -> std::string_view {
  switch (kind) {
    case AudienceKind::everyone:
      return "everyone";
    case AudienceKind::learner:
      return "learner";
    case AudienceKind::administrator:
      return "administrator";
    case AudienceKind::team:
      return "team";
  }
  return "unknown";
}

auto kind_of(std::int64_t value) -> AudienceKind {
  switch (value) {
    case LEARNER_KIND:
      return AudienceKind::learner;
    case ADMINISTRATOR_KIND:
      return AudienceKind::administrator;
    default:
      return AudienceKind::team;
  }
}

auto refuse(std::string_view message) -> Error {
  return Error::make(ErrorCode::invalid_argument, message);
}

}  // namespace

auto everyone() -> std::int64_t { return 0; }

auto identifier_limit() -> std::int64_t { return IDENTIFIER_LIMIT; }

auto encode(AudienceKind kind, std::int64_t id) -> Result<std::int64_t> {
  if (kind == AudienceKind::everyone) {
    if (id != 0) {
      return std::unexpected(refuse("everyone carries no identifier; pass id 0"));
    }
    return everyone();
  }
  if (id < 0 || id > IDENTIFIER_LIMIT) {
    return std::unexpected(refuse("the " + std::string{name(kind)} + " id " + std::to_string(id) +
                                  " is outside [0, " + std::to_string(IDENTIFIER_LIMIT) + "]"));
  }
  switch (kind) {
    case AudienceKind::learner:
      return LEARNER_KIND * SPAN + id;
    case AudienceKind::administrator:
      return ADMINISTRATOR_KIND * SPAN + id;
    case AudienceKind::team:
      return TEAM_KIND * SPAN + id;
    case AudienceKind::everyone:
      break;
  }
  return std::unexpected(refuse("unknown audience kind " + std::to_string(static_cast<int>(kind))));
}

auto decode(std::int64_t value) -> Result<Principal> {
  if (value == everyone()) {
    return Principal{.kind = AudienceKind::everyone, .id = 0};
  }
  if (value < SPAN) {
    return std::unexpected(refuse("principal value " + std::to_string(value) +
                                  " is neither everyone nor inside an encoded kind range"));
  }
  const auto kind = value / SPAN;
  if (kind > TEAM_KIND) {
    return std::unexpected(refuse("principal value " + std::to_string(value) +
                                  " carries kind " + std::to_string(kind) + ", outside 1..3"));
  }
  return Principal{.kind = kind_of(kind), .id = value % SPAN};
}

auto audience_for(std::int64_t viewer_id, std::span<const std::int64_t> team_ids)
    -> Result<Audience> {
  auto audience = Audience{};
  audience.reserve(2 + team_ids.size());
  audience.push_back(everyone());
  const auto viewer = encode(AudienceKind::learner, viewer_id);
  if (!viewer) {
    return std::unexpected(viewer.error());
  }
  audience.push_back(*viewer);
  for (const auto team_id : team_ids) {
    const auto team = encode(AudienceKind::team, team_id);
    if (!team) {
      return std::unexpected(team.error());
    }
    audience.push_back(*team);
  }
  return audience;
}

auto default_visibility(AuthorKind author) -> Audience {
  if (author == AuthorKind::learner) {
    return Audience{everyone()};
  }
  return Audience{};
}

auto serial_id_of(db::connection& database, std::string_view principal_id)
    -> Result<std::int64_t> {
  for (const auto& row : database(sqlpp::select(db::principals.serialId)
                                      .from(db::principals)
                                      .where(db::principals.id == std::string{principal_id}))) {
    return static_cast<std::int64_t>(row.serialId);
  }
  return std::unexpected(Error::make(
      ErrorCode::not_found,
      "no principal row for " + std::string{principal_id} + "; refusing to compose an audience"));
}

auto request_audience(db::connection& database, std::string_view principal_id)
    -> Result<Audience> {
  const auto serial = serial_id_of(database, principal_id);
  if (!serial) {
    return std::unexpected(serial.error());
  }
  return audience_for(*serial, std::span<const std::int64_t>{});
}

}  // namespace cms::bank
