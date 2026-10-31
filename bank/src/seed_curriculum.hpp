#pragma once

#include <cms/bank/bank.hpp>
#include <cms/db/connection.hpp>
#include <cms/error.hpp>

namespace cms::bank {

/// @brief Imports the curriculum, writing only the tracks whose digest moved.
///
/// @param database The connection; the caller owns the transaction.
/// @param bank The bank whose tracks are imported.
/// @return The number of tracks written, or the first refusal.
///
/// A track the data no longer declares is removed, but only when the seeder owns it: an administrator's
/// route survives an import, the same rule tasks follow.
[[nodiscard]] Result<std::size_t> import_curriculum(db::connection& database, const Bank& bank);

}  // namespace cms::bank
