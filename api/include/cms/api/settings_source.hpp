#pragma once

#include <memory>

#include <cms/db/connection.hpp>
#include <cms/error.hpp>
#include <cms/settings_snapshot.hpp>

namespace cms::api {

/// @brief Reads every settings row and the version counter into one immutable snapshot.
///
/// @param database The connector to read through.
/// @return The snapshot, or an error when a row contradicts its declared kind or the version row is
/// missing while settings rows exist.
///
/// The version comes from the row the trigger maintains, not from a count of the rows returned: a
/// transaction that adds, removes and edits still moves the counter exactly once.
[[nodiscard]] Result<std::shared_ptr<const SettingsSnapshot>> load_settings(db::connection& database);

}  // namespace cms::api
