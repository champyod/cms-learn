#pragma once

#include <string_view>

#include <cms/db/connection.hpp>

namespace cms::api {

/// @brief Writes one append-only audit row.
///
/// @param database The connection; the row lands in the caller's transaction, so an action and its
/// record commit together or not at all.
/// @param actor_id The principal the action is attributed to.
/// @param invoked_by How it was invoked — the surface, not the actor.
/// @param action The dotted action name the audit table's own pattern accepts.
/// @param subject What the action was performed on.
///
/// There is one definition because the audit table is append-only: a second writer with a slightly
/// different column set would be a way to record an action that never happened.
void record_audit(db::connection& database, std::string_view actor_id, std::string_view invoked_by,
                  std::string_view action, std::string_view subject);

}  // namespace cms::api
