#include <cms/api/authorize.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <sqlpp23/sqlpp23.h>

#include <cms/db/tables.hpp>

namespace cms::api {
namespace {

auto role_ids_of(db::connection& database, std::string_view principal_id)
    -> std::vector<std::string> {
  auto ids = std::vector<std::string>{};
  for (const auto& row : database(sqlpp::select(db::principalRoles.roleId)
                                      .from(db::principalRoles)
                                      .where(db::principalRoles.principalId ==
                                             std::string{principal_id}))) {
    ids.emplace_back(row.roleId);
  }
  return ids;
}

}  // namespace

auto load_capabilities(db::connection& database, std::string_view principal_id)
    -> Result<CapabilitySet> {
  auto set = CapabilitySet{};
  try {
    auto kind = std::string{};
    for (const auto& row : database(sqlpp::select(db::principals.kind)
                                        .from(db::principals)
                                        .where(db::principals.id == std::string{principal_id}))) {
      kind = std::string{row.kind};
    }
    if (kind.empty()) {
      return std::unexpected(
          Error::make(ErrorCode::not_found, "no such principal: " + std::string{principal_id}));
    }
    if (kind == "system") {
      return all_capabilities();
    }

    for (const auto& role_id : role_ids_of(database, principal_id)) {
      for (const auto& row : database(sqlpp::select(db::roleCapabilities.capability)
                                          .from(db::roleCapabilities)
                                          .where(db::roleCapabilities.roleId == role_id))) {
        // A stored name the core does not define grants nothing: an unknown value must never widen
        // authority, and the database's enum is what keeps it from being stored in the first place.
        if (const auto known = capability_from_name(row.capability)) {
          set.grant(*known);
        }
      }
    }
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }
  return set;
}

auto authorize(db::connection& database, std::string_view principal_id, Capability capability)
    -> Result<void> {
  const auto held = load_capabilities(database, principal_id);
  if (!held) {
    return std::unexpected(held.error());
  }
  if (!held->has(capability)) {
    return std::unexpected(Error::make(
        ErrorCode::permission_denied,
        "this account does not hold " + std::string{capability_name(capability)}));
  }
  return {};
}

}  // namespace cms::api
