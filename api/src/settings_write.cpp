#include <cms/api/settings_write.hpp>

#include <cms/seal.hpp>

#include <algorithm>
#include <cctype>
#include <exception>
#include <string>
#include <string_view>

#include <sqlpp23/sqlpp23.h>

#include <cms/api/uuid.hpp>

#include <cms/db/tables.hpp>

namespace cms::api {

namespace {

constexpr std::string_view SECRET_TIER = "secret";

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
           const Actor& actor) -> void {
  if (setting_exists(database, entry.key)) {
    database(sqlpp::update(db::settings)
                 .set(db::settings.valueText = std::string{value},
                      db::settings.kind = entry.kind, db::settings.tier = entry.tier,
                      db::settings.updatedAt = db::now_stamp(),
                      db::settings.actorKind = actor.kind,
                      db::settings.learnerActorId = actor_id_of(actor, ACTOR_KIND_LEARNER),
                      db::settings.adminActorId = actor_id_of(actor, ACTOR_KIND_ADMIN),
                      db::settings.systemActorId = actor_id_of(actor, ACTOR_KIND_SYSTEM))
                 .where(db::settings.key == entry.key));
    return;
  }
  database(sqlpp::insert_into(db::settings).set(
      db::settings.key = entry.key, db::settings.kind = entry.kind,
      db::settings.valueText = std::string{value}, db::settings.tier = entry.tier,
      db::settings.actorKind = actor.kind,
      db::settings.learnerActorId = actor_id_of(actor, ACTOR_KIND_LEARNER),
      db::settings.adminActorId = actor_id_of(actor, ACTOR_KIND_ADMIN),
      db::settings.systemActorId = actor_id_of(actor, ACTOR_KIND_SYSTEM)));
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

auto writes_a_secret(const std::vector<CatalogueEntry>& catalogue,
                     const std::vector<SettingChange>& changes) -> bool {
  return std::ranges::any_of(changes, [&catalogue](const SettingChange& change) {
    const auto* entry = entry_for(catalogue, change.key);
    return entry != nullptr && entry->tier == SECRET_TIER;
  });
}

/// @brief Turns a form value into what the row may hold: a secret is sealed here, and an empty secret keeps the value
/// that is already stored.
///
/// This is the one function that writes a settings row, so the seal lives here rather than in the route: every caller —
/// the settings form, a plan, a test fixture — passes through it, and the schema's `settings_secret_is_sealed` CHECK is
/// the second line of defence rather than the only one.
auto stored_changes(const std::vector<CatalogueEntry>& catalogue, const std::vector<SettingChange>& changes)
    -> Result<std::vector<SettingChange>> {
  auto stored = std::vector<SettingChange>{};
  for (const auto& change : changes) {
    const auto* entry = entry_for(catalogue, change.key);
    if (entry == nullptr || entry->tier != SECRET_TIER) {
      stored.push_back(change);
      continue;
    }
    if (change.value.empty()) {
      continue;
    }
    const auto sealer = cms::seal::sealer();
    if (!sealer) {
      return std::unexpected(sealer.error());
    }
    const auto sealed = sealer->seal(change.value);
    if (!sealed) {
      return std::unexpected(sealed.error());
    }
    stored.push_back(SettingChange{.key = change.key, .value = *sealed});
  }
  return stored;
}

auto apply_settings(db::connection& database, const std::vector<CatalogueEntry>& catalogue,
                    const std::vector<SettingChange>& changes, const Actor& actor)
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

  if (!is_actor_kind(actor.kind)) {
    return std::unexpected(Error::make(
        ErrorCode::invalid_argument, "a settings write needs a known actor kind, not \"" +
                                         actor.kind + "\""));
  }
  if (!has_uuid_shape(actor.id)) {
    return std::unexpected(
        Error::make(ErrorCode::invalid_argument, "a settings write needs the actor's id"));
  }

  try {
    // The settings trigger reads the actor from the session, and `SET LOCAL` cannot bind a parameter, so
    // the known kind and the id's checked shape are what make the pasted literals safe.
    const auto set_actor_id = sqlpp::verbatim("SET LOCAL cms.actor_id = '" + actor.id + "'");
    const auto set_actor_kind = sqlpp::verbatim("SET LOCAL cms.actor_kind = '" + actor.kind + "'");
    for (const auto& statement : {set_actor_id, set_actor_kind}) {
      const auto rows_set = database.execute(statement, db::bindings_for(statement));
      (void)rows_set;
    }

    const auto stored = stored_changes(catalogue, changes);
    if (!stored) {
      return std::unexpected(stored.error());
    }
    for (const auto& change : *stored) {
      const auto* entry = entry_for(catalogue, change.key);
      store(database, *entry, change.value, actor);
    }
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }
  return {};
}

}  // namespace cms::api
