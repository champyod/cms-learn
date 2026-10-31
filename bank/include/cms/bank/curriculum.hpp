#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <cms/bank/bank.hpp>
#include <cms/db/connection.hpp>
#include <cms/error.hpp>

namespace cms::bank {

/// @brief Reads the curriculum the database holds.
///
/// @param database The connection to read through.
/// @return Every track with its stages and their tags, in the order the data declares.
///
/// There is no branch on a track id anywhere: the rows decide what exists, so a third route is a
/// content change and not a code change. Three queries feed this — tracks, stages, tags — and the
/// assembly is a lookup per row rather than a query per stage.
[[nodiscard]] Result<std::vector<Track>> read_curriculum(db::connection& database);

}  // namespace cms::bank
