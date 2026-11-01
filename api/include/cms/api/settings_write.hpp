#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <cms/api/settings_view.hpp>
#include <cms/db/connection.hpp>
#include <cms/error.hpp>

namespace cms::api {

/// @brief One change an operator asked for.
struct SettingChange {
  std::string key;
  std::string value;
};

/// @brief Whether @p value can be stored for a setting of @p kind.
///
/// The table enforces the same rule; this says which key broke it. Shared with the plan path so a value
/// is refused at planning time with the message the write path would give, rather than by a second
/// opinion that can drift from this one.
[[nodiscard]] bool matches_kind(std::string_view kind, std::string_view value);

/// @brief Applies catalogued changes to the settings table.
///
/// @param database The connection; the caller owns the transaction.
/// @param catalogue The keys this build understands.
/// @param changes The values to store.
/// @param actor_id The principal the change is attributed to.
/// @return Success, or the first change that was refused.
///
/// A key that is not in the catalogue is refused rather than created: the catalogue is the only place a key
/// comes into existence, and a table row cannot widen it. A value that does not match its declared kind is
/// refused before the write, so the table's own constraint is the backstop rather than the explanation.
///
/// A secret may be written here — that is how a credential is rotated — and the audit trigger stores
/// `<redacted>` for it. Nothing on this path may return the value it was given.
[[nodiscard]] Result<void> apply_settings(db::connection& database,
                                          const std::vector<CatalogueEntry>& catalogue,
                                          const std::vector<SettingChange>& changes,
                                          std::string_view actor_id);

}  // namespace cms::api
