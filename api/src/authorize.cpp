#include <cms/api/authorize.hpp>

#include <cms/api/elevation.hpp>

#include <cstddef>
#include <exception>
#include <string>
#include <string_view>
#include <vector>

#include <sqlpp23/sqlpp23.h>

#include <cms/db/tables.hpp>

namespace cms::api {
namespace {

constexpr std::string_view LEARNER_KIND = "learner";
constexpr std::string_view SYSTEM_KIND = "system";

auto role_ids_of(db::connection& database, std::string_view admin_id) -> std::vector<std::string> {
  auto ids = std::vector<std::string>{};
  for (const auto& row : database(sqlpp::select(db::adminRoles.roleId)
                                      .from(db::adminRoles)
                                      .where(db::adminRoles.adminId == std::string{admin_id}))) {
    ids.emplace_back(row.roleId);
  }
  return ids;
}

}  // namespace

auto load_capabilities(db::connection& database, std::string_view account_kind,
                       std::string_view account_id) -> Result<CapabilitySet> {
  auto set = CapabilitySet{};
  try {
    // A learner holds no capabilities at all: its authority is its own progress and nothing else.
    if (account_kind == LEARNER_KIND) {
      return set;
    }
    if (account_kind == SYSTEM_KIND) {
      return all_capabilities();
    }

    auto is_admin = false;
    for (const auto& row : database(sqlpp::select(db::adminAccounts.id)
                                        .from(db::adminAccounts)
                                        .where(db::adminAccounts.id == std::string{account_id}))) {
      (void)row;
      is_admin = true;
    }
    if (!is_admin) {
      return std::unexpected(Error::make(ErrorCode::not_found,
                                         "no such administrator: " + std::string{account_id}));
    }

    for (const auto& role_id : role_ids_of(database, account_id)) {
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

auto authorize(db::connection& database, std::string_view account_kind,
               std::string_view account_id, Capability capability) -> Result<void> {
  const auto held = load_capabilities(database, account_kind, account_id);
  if (!held) {
    return std::unexpected(held.error());
  }
  if (held->has(capability)) {
    return {};
  }
  // A live elevation is authority the same account holds for a few minutes, approved by a second
  // administrator; without this read the two-person rule grants nothing.
  const auto elevated = load_elevated_capabilities(database, account_id);
  if (!elevated) {
    return std::unexpected(elevated.error());
  }
  if (!elevated->has(capability)) {
    return std::unexpected(Error::make(
        ErrorCode::permission_denied,
        "this account does not hold " + std::string{capability_name(capability)}));
  }
  const auto spent = spend_elevation(database, account_id, capability);
  if (!spent) {
    return std::unexpected(spent.error());
  }
  if (!*spent) {
    return std::unexpected(Error::make(
        ErrorCode::permission_denied,
        "the approval for " + std::string{capability_name(capability)} + " was already used"));
  }
  return {};
}

}  // namespace cms::api
