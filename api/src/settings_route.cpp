#include <cms/api/settings_route.hpp>

#include <cstddef>
#include <exception>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <sqlpp23/sqlpp23.h>

#include <cms/api/form_body.hpp>
#include <cms/db/tables.hpp>
#include <cms/db/transaction.hpp>

namespace cms::api {
namespace {

}  // namespace

auto principal_of_session(db::connection& database, std::string_view session_id)
    -> Result<std::string> {
  // An absent cookie is no session at all, and the id column is a uuid: comparing it with an empty string
  // raises rather than returning no rows.
  if (session_id.empty()) {
    return std::string{};
  }
  try {
    for (const auto& row : database(sqlpp::select(db::sessions.principalId)
                                        .from(db::sessions)
                                        .where(db::sessions.id == std::string{session_id} &&
                                               db::sessions.revokedAt.is_null() &&
                                               db::sessions.expiresAt > db::now_stamp()))) {
      return std::string{row.principalId};
    }
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }
  return std::string{};
}

auto setting_changes(std::string_view body, const std::vector<CatalogueEntry>& catalogue)
    -> std::vector<SettingChange> {
  auto changes = std::vector<SettingChange>{};
  auto start = std::size_t{0};
  while (start < body.size()) {
    const auto end = body.find('&', start);
    const auto pair = body.substr(start, end == std::string_view::npos ? end : end - start);
    const auto separator = pair.find('=');
    if (separator != std::string_view::npos) {
      const auto key = pair.substr(0, separator);
      if (is_catalogued(catalogue, key)) {
        changes.push_back(SettingChange{.key = std::string{key}, .value = form_field(pair, key)});
      }
    }
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1;
  }
  return changes;
}

auto commit_settings(const std::shared_ptr<const db::connection_config>& config,
                     const std::vector<CatalogueEntry>& catalogue,
                     const std::vector<SettingChange>& changes, std::string_view actor_id)
    -> Result<void> {
  try {
    auto transaction = cms::db::Transaction{*config->client};
    const auto applied = apply_settings(transaction.connection(), catalogue, changes, actor_id);
    if (!applied) {
      return std::unexpected(applied.error());
    }
    transaction.commit();
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }
  return {};
}

}  // namespace cms::api
