#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <sqlpp23/sqlpp23.h>

#include <cms/api/actor.hpp>
#include <cms/db/connection.hpp>
#include <cms/db/tables.hpp>
#include <cms/error.hpp>

namespace cms::api::detail {

SQLPP_CREATE_NAME_TAG(row_count);

constexpr std::string_view ACTOR_ADMIN = "admin";
constexpr std::string_view ACTOR_SYSTEM = "system";

auto any_row(db::connection& database, const auto& statement) -> bool {
  auto found = false;
  for (const auto& row : database(statement)) {
    found = row.row_count > 0;
  }
  return found;
}

inline auto unknown_actor(const Actor& actor) -> std::optional<Error> {
  if (actor.kind == ACTOR_ADMIN || actor.kind == ACTOR_SYSTEM) {
    return std::nullopt;
  }
  return Error::make(ErrorCode::invalid_argument, "unknown account kind: " + actor.kind);
}

inline auto is_admin_actor(const Actor& actor) -> bool { return actor.kind == ACTOR_ADMIN; }

inline auto learner_exists(db::connection& database, std::string_view learner_id) -> bool {
  const auto one = sqlpp::value(1);
  return any_row(database, sqlpp::select(sqlpp::count(one).as(row_count))
                               .from(db::learnerAccounts)
                               .where(db::learnerAccounts.id == std::string{learner_id}));
}

inline auto learner_has_email(db::connection& database, std::string_view learner_id) -> bool {
  const auto one = sqlpp::value(1);
  return any_row(database, sqlpp::select(sqlpp::count(one).as(row_count))
                               .from(db::learnerAccounts)
                               .where(db::learnerAccounts.id == std::string{learner_id} &&
                                      db::learnerAccounts.email.is_not_null()));
}

inline auto role_exists(db::connection& database, std::string_view role_id) -> bool {
  const auto one = sqlpp::value(1);
  return any_row(database, sqlpp::select(sqlpp::count(one).as(row_count))
                               .from(db::roles)
                               .where(db::roles.id == std::string{role_id}));
}

inline auto admin_holds_role(db::connection& database, std::string_view admin_id,
                             std::string_view role_id) -> bool {
  const auto one = sqlpp::value(1);
  return any_row(database, sqlpp::select(sqlpp::count(one).as(row_count))
                               .from(db::adminRoles)
                               .where(db::adminRoles.adminId == std::string{admin_id} &&
                                      db::adminRoles.roleId == std::string{role_id}));
}

inline auto refused_unheld_role(db::connection& database, const Actor& actor,
                                std::string_view role_id) -> std::optional<Error> {
  // The system account holds every capability, so every existing role is within its power.
  if (actor.kind == ACTOR_SYSTEM) {
    if (!role_exists(database, role_id)) {
      return Error::make(ErrorCode::not_found, "no such role: " + std::string{role_id});
    }
    return std::nullopt;
  }
  if (!admin_holds_role(database, actor.id, role_id)) {
    return Error::make(ErrorCode::permission_denied, "this account does not hold the role " +
                                                        std::string{role_id} +
                                                        ", so it cannot grant it");
  }
  return std::nullopt;
}

}  // namespace cms::api::detail
