#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <cms/api/settings_view.hpp>
#include <cms/api/settings_write.hpp>
#include <cms/db/connection.hpp>
#include <cms/error.hpp>

namespace cms::api {

/// @brief The principal the session cookie names.
///
/// @param database The connection to read through.
/// @param session_id The opaque cookie value, which is the session's primary key.
/// @return The principal id, an empty string when no live session carries that id, or the reason the read
/// failed.
///
/// A row that has expired or been revoked is not a session, so the condition lives in this query and every
/// route that resolves a cookie inherits it rather than restating it.
[[nodiscard]] Result<std::string> principal_of_session(db::connection& database,
                                                       std::string_view session_id);

/// @brief The changes a settings form carries, one per catalogued field.
///
/// @param body The raw `application/x-www-form-urlencoded` body.
/// @param catalogue The keys this build knows.
/// @return One change per field the catalogue names, in the order the form sent them.
///
/// A field the catalogue does not name is skipped rather than refused: a page from another build may carry
/// a control this one does not understand, and refusing the whole form would discard the fields it does.
/// A catalogued field whose value is wrong for its kind still fails, because apply_settings refuses it
/// before anything is written.
[[nodiscard]] std::vector<SettingChange> setting_changes(
    std::string_view body, const std::vector<CatalogueEntry>& catalogue);

/// @brief Applies @p changes in one transaction opened on @p config's client.
///
/// @param config The connection the service was started with.
/// @param catalogue The keys this build knows.
/// @param changes The values to store.
/// @param actor_id The principal the change is attributed to.
/// @return Success, or the refusal apply_settings produced, or the reason the transaction failed.
///
/// apply_settings names the actor with `SET LOCAL`, which lasts only as long as the transaction it runs in,
/// so the caller has to be inside one: this is the transaction, and committing it is what lets the stamped
/// rows and the actor they name land together.
[[nodiscard]] Result<void> commit_settings(const std::shared_ptr<const db::connection_config>& config,
                                           const std::vector<CatalogueEntry>& catalogue,
                                           const std::vector<SettingChange>& changes,
                                           std::string_view actor_id);

}  // namespace cms::api
