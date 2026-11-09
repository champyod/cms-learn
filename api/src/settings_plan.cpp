#include <cms/api/settings_plan.hpp>

#include "settings_plan_state.hpp"

#include <exception>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <cms/api/settings_view.hpp>
#include <cms/api/settings_write.hpp>
#include <cms/api/uuid.hpp>
#include <cms/db/tables.hpp>
#include <cms/db/transaction.hpp>

namespace cms::api {

using detail::apply_inside;
using detail::changes_text;
using detail::entry_for;
using detail::SECRET_TIER;
using detail::stored_value;

auto plan_setting(db::connection& database, const std::vector<CatalogueEntry>& catalogue,
                  std::string_view key, std::string_view value, const Actor& author)
    -> Result<PlanPreview> {
  const auto* entry = entry_for(catalogue, key);
  if (entry == nullptr) {
    return std::unexpected(Error::make(ErrorCode::invalid_argument,
                                       "\"" + std::string{key} + "\" is not a setting this build "
                                                                 "knows"));
  }
  if (!matches_kind(entry->kind, value)) {
    return std::unexpected(
        Error::make(ErrorCode::invalid_argument, std::string{key} + " expects a " + entry->kind));
  }
  if (!is_actor_kind(author.kind)) {
    return std::unexpected(Error::make(
        ErrorCode::invalid_argument,
        "a plan needs a known author kind, not \"" + author.kind + "\""));
  }
  if (!has_uuid_shape(author.id)) {
    return std::unexpected(Error::make(ErrorCode::invalid_argument, "a plan needs the author's id"));
  }

  const auto stored = stored_value(database, key);
  const auto id = uuid_v4();
  auto expires_at = cms::db::timestamp{};
  try {
    database(sqlpp::insert_into(db::settingsPlans).set(
        db::settingsPlans.id = id, db::settingsPlans.actorKind = author.kind,
        db::settingsPlans.learnerActorId = actor_id_of(author, ACTOR_KIND_LEARNER),
        db::settingsPlans.adminActorId = actor_id_of(author, ACTOR_KIND_ADMIN),
        db::settingsPlans.systemActorId = actor_id_of(author, ACTOR_KIND_SYSTEM),
        db::settingsPlans.changes = changes_text(key, value)));
    // The expiry is the table's default, so it is read back rather than restated here.
    for (const auto& row : database(sqlpp::select(db::settingsPlans.expiresAt)
                                        .from(db::settingsPlans)
                                        .where(db::settingsPlans.id == id))) {
      expires_at = row.expiresAt;
    }
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }

  const auto previous = entry->tier == SECRET_TIER
                            ? std::string{REDACTED}
                            : (stored.is_present ? stored.text : entry->fallback);
  return PlanPreview{.id = id,
                     .key = std::string{key},
                     .old_value = previous,
                     .new_value = std::string{value},
                     .tier = entry->tier,
                     .reload = entry->reload,
                     .expires_at = cms::db::format_timestamp(expires_at)};
}

auto apply_plan(const std::shared_ptr<const db::connection_config>& config,
                const std::vector<CatalogueEntry>& catalogue, std::string_view plan_id,
                const Actor& actor) -> Result<AppliedPlan> {
  if (!has_uuid_shape(plan_id)) {
    return std::unexpected(Error::make(ErrorCode::not_found, "no plan " + std::string{plan_id}));
  }
  if (!is_actor_kind(actor.kind)) {
    return std::unexpected(Error::make(
        ErrorCode::invalid_argument,
        "an apply needs a known actor kind, not \"" + actor.kind + "\""));
  }
  if (!has_uuid_shape(actor.id)) {
    return std::unexpected(Error::make(ErrorCode::invalid_argument, "an apply needs the actor's id"));
  }
  try {
    auto transaction = cms::db::Transaction{*config->client};
    const auto applied = apply_inside(transaction.connection(), catalogue, plan_id, actor);
    if (!applied) {
      return std::unexpected(applied.error());
    }
    transaction.commit();
    return applied;
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }
}

}  // namespace cms::api
