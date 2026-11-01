#include <cms/api/settings_write.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <exception>
#include <string>
#include <string_view>

#include <sqlpp23/sqlpp23.h>

#include <cms/api/uuid.hpp>
#include <cms/db/tables.hpp>

namespace cms::api {
namespace {

auto now_stamp() -> cms::db::timestamp {
  return std::chrono::time_point_cast<cms::db::timestamp::duration>(
      std::chrono::system_clock::now());
}

auto is_integer(std::string_view text) -> bool {
  if (text.empty()) {
    return false;
  }
  const auto start = text.front() == '-' ? std::string_view{text}.substr(1) : text;
  return !start.empty() &&
         std::ranges::all_of(start, [](char character) {
           return std::isdigit(static_cast<unsigned char>(character)) != 0;
         });
}

auto entry_for(const std::vector<CatalogueEntry>& catalogue, std::string_view key)
    -> const CatalogueEntry* {
  const auto found = std::ranges::find(catalogue, key, &CatalogueEntry::key);
  return found == catalogue.end() ? nullptr : &*found;
}

/// A 36-character UUID, checked before the value is placed in a `SET LOCAL` statement: a session variable
/// cannot be bound as a parameter, so the shape is the guard that makes the literal safe.
auto setting_exists(db::connection& database, std::string_view key) -> bool {
  for (const auto& row :
       database(sqlpp::select(db::settings.key)
                    .from(db::settings)
                    .where(db::settings.key == std::string{key}))) {
    (void)row;
    return true;
  }
  return false;
}

auto store(db::connection& database, const CatalogueEntry& entry, std::string_view value,
           std::string_view actor_id) -> void {
  if (setting_exists(database, entry.key)) {
    database(sqlpp::update(db::settings)
                 .set(db::settings.valueText = std::string{value},
                      db::settings.kind = entry.kind, db::settings.tier = entry.tier,
                      db::settings.updatedAt = now_stamp(),
                      db::settings.updatedBy = std::string{actor_id})
                 .where(db::settings.key == entry.key));
    return;
  }
  database(sqlpp::insert_into(db::settings).set(
      db::settings.key = entry.key, db::settings.kind = entry.kind,
      db::settings.valueText = std::string{value}, db::settings.tier = entry.tier,
      db::settings.updatedBy = std::string{actor_id}));
}

}  // namespace

auto matches_kind(std::string_view kind, std::string_view value) -> bool {
  if (kind == "bool") {
    return value == "true" || value == "false";
  }
  if (kind == "int") {
    return is_integer(value);
  }
  return kind == "text";
}

auto apply_settings(db::connection& database, const std::vector<CatalogueEntry>& catalogue,
                    const std::vector<SettingChange>& changes, std::string_view actor_id)
    -> Result<void> {
  // Everything is validated before anything is written: a form that carries one bad row must not leave the
  // rest of it applied.
  for (const auto& change : changes) {
    const auto* entry = entry_for(catalogue, change.key);
    if (entry == nullptr) {
      return std::unexpected(Error::make(ErrorCode::invalid_argument,
                                         "\"" + change.key + "\" is not a setting this build knows"));
    }
    if (!matches_kind(entry->kind, change.value)) {
      return std::unexpected(Error::make(ErrorCode::invalid_argument,
                                         change.key + " expects a " + entry->kind));
    }
  }

  try {
    // The settings trigger reads the actor from the session; an unset actor is a refusal, not a default. It is
    // set once per transaction, before any row is touched.
    // The actor is pasted into `SET LOCAL`, which cannot bind a parameter, so its shape is what makes the
    // literal safe.
    if (!has_uuid_shape(actor_id)) {
      return std::unexpected(
          Error::make(ErrorCode::invalid_argument, "a settings write needs the actor's id"));
    }
    // `SET LOCAL` is not a statement sqlpp23 models, so it goes through the connector's execute with an
    // empty binding list rather than through operator(), which requires a statement type it can dispatch.
    const auto set_actor = sqlpp::verbatim("SET LOCAL cms.actor_id = '" + std::string{actor_id} + "'");
    const auto rows_set = database.execute(set_actor, db::bindings_for(set_actor));
    (void)rows_set;

    for (const auto& change : changes) {
      const auto* entry = entry_for(catalogue, change.key);
      store(database, *entry, change.value, actor_id);
    }
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }
  return {};
}

}  // namespace cms::api
