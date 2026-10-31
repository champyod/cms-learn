#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <cms/bank/bank.hpp>
#include <cms/db/connection.hpp>
#include <cms/error.hpp>

namespace cms::bank {

/// @brief What one import did.
struct SeedReport {
  /// False when the bank's digest matched the stored one, in which case nothing was written at all.
  bool is_changed{false};
  /// The content version after the call. It moves only when the bank moved.
  std::int64_t version{0};
  std::size_t topics_written{0};
  std::size_t tasks_inserted{0};
  std::size_t tasks_updated{0};
  /// Tasks the seeder left alone because an administrator owns them.
  std::size_t tasks_kept{0};
};

/// @brief Imports @p bank, writing only what changed.
///
/// @param database The connection; the caller owns the transaction, so an interrupted import leaves
/// the previous version in place rather than half of the new one.
/// @param bank The bank to import.
/// @return What the import did, or the first refusal.
///
/// The digest of the whole bank is compared with the stored one before any statement runs: an unchanged
/// bank costs one read and writes nothing, which is what makes running the seeder on every start safe.
[[nodiscard]] Result<SeedReport> seed(db::connection& database, const Bank& bank);

}  // namespace cms::bank
