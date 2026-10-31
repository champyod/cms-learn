#pragma once

#include <string>
#include <string_view>

#include <cms/capability.hpp>
#include <cms/db/connection.hpp>
#include <cms/error.hpp>

namespace cms::api {

/// @brief Loads the capabilities a principal holds, through its roles.
///
/// A system principal holds every capability the core defines, which is why "root can do anything" is
/// answerable by reading the enumeration rather than by a rule. Any stored name the core does not know
/// is ignored rather than invented, so a row cannot widen authority.
/// @param database The connection to read through.
/// @param principal_id The account whose authority is being loaded.
[[nodiscard]] Result<CapabilitySet> load_capabilities(db::connection& database,
                                                     std::string_view principal_id);

/// @brief Checks that @p principal_id holds @p capability, and explains the refusal when it does not.
/// @return Success, or a permission_denied error naming the capability.
[[nodiscard]] Result<void> authorize(db::connection& database, std::string_view principal_id,
                                     Capability capability);

}  // namespace cms::api
