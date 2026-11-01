#include <cms/api/settings_plan.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <exception>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <simdjson.h>
#include <sqlpp23/sqlpp23.h>

#include <cms/api/settings_write.hpp>
#include <cms/api/uuid.hpp>
#include <cms/db/tables.hpp>
#include <cms/db/transaction.hpp>

namespace cms::api {
namespace {

constexpr std::string_view SECRET_TIER = "secret";
/// The first code point JSON cannot carry literally; it needs the `\u` form instead.
constexpr unsigned char LOWEST_CONTROL_CHARACTER = 0x20;

auto now_stamp() -> cms::db::timestamp {
  return std::chrono::time_point_cast<cms::db::timestamp::duration>(
      std::chrono::system_clock::now());
}

auto entry_for(const std::vector<CatalogueEntry>& catalogue, std::string_view key)
    -> const CatalogueEntry* {
  const auto found = std::ranges::find(catalogue, key, &CatalogueEntry::key);
  return found == catalogue.end() ? nullptr : &*found;
}

/// A JSON string literal for @p text. The key is already constrained by the schema's own pattern, so the
/// value of a text setting — which may hold anything — is what this escaping has to carry.
auto json_string(std::string_view text) -> std::string {
  auto quoted = std::string{};
  quoted.reserve(text.size() + 2);
  quoted.push_back('"');
  for (const auto character : text) {
    switch (character) {
      case '"':
        quoted += "\\\"";
        break;
      case '\\':
        quoted += "\\\\";
        break;
      case '\b':
        quoted += "\\b";
        break;
      case '\f':
        quoted += "\\f";
        break;
      case '\n':
        quoted += "\\n";
        break;
      case '\r':
        quoted += "\\r";
        break;
      case '\t':
        quoted += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(character) < LOWEST_CONTROL_CHARACTER) {
          quoted += std::format("\\u{:04x}", static_cast<unsigned>(character));
        } else {
          quoted.push_back(character);
        }
    }
  }
  quoted.push_back('"');
  return quoted;
}

auto changes_text(std::string_view key, std::string_view value) -> std::string {
  return "{\"key\":" + json_string(key) + ",\"value\":" + json_string(value) + "}";
}

struct PlanChanges {
  std::string key;
  std::string value;
};

auto parse_changes(std::string_view text) -> Result<PlanChanges> {
  const auto json = simdjson::padded_string{text};
  auto parser = simdjson::dom::parser{};
  auto document = simdjson::dom::element{};
  if (parser.parse(json).get(document) != simdjson::SUCCESS ||
      document.type() != simdjson::dom::element_type::OBJECT) {
    return std::unexpected(
        Error::make(ErrorCode::internal, "a stored plan is not one JSON object"));
  }
  auto key = std::string_view{};
  auto value = std::string_view{};
  if (document.at_key("key").get_string().get(key) != simdjson::SUCCESS ||
      document.at_key("value").get_string().get(value) != simdjson::SUCCESS) {
    return std::unexpected(
        Error::make(ErrorCode::internal, "a stored plan names no key and value"));
  }
  return PlanChanges{.key = std::string{key}, .value = std::string{value}};
}

struct StoredValue {
  bool is_present{false};
  std::string text;
};

auto stored_value(db::connection& database, std::string_view key) -> StoredValue {
  auto stored = StoredValue{};
  for (const auto& row : database(sqlpp::select(db::settings.valueText)
                                      .from(db::settings)
                                      .where(db::settings.key == std::string{key}))) {
    stored.is_present = true;
    stored.text = std::string{row.valueText};
  }
  return stored;
}

/// Whether the key was written at or after @p moment. Both timestamps are the database's own `now()`, so
/// the comparison does not depend on the service host's clock agreeing with the server's. A write in the
/// same microsecond as the plan cannot be ordered against it, so it counts as later: refusing sends the
/// operator back to a fresh diff, which is the cheap direction to be wrong in.
auto written_since(db::connection& database, std::string_view key, cms::db::timestamp moment)
    -> bool {
  for (const auto& row : database(sqlpp::select(db::settingsAudit.id)
                                      .from(db::settingsAudit)
                                      .where(db::settingsAudit.key == std::string{key} &&
                                             db::settingsAudit.occurredAt >= moment))) {
    (void)row;
    return true;
  }
  return false;
}

struct PlanRow {
  std::string changes;
  cms::db::timestamp created_at{};
  cms::db::timestamp expires_at{};
  bool is_applied{false};
  bool is_found{false};
};

auto find_plan(db::connection& database, std::string_view plan_id) -> PlanRow {
  auto row = PlanRow{};
  for (const auto& record :
       database(sqlpp::select(db::settingsPlans.changes, db::settingsPlans.createdAt,
                              db::settingsPlans.expiresAt, db::settingsPlans.appliedAt)
                    .from(db::settingsPlans)
                    .where(db::settingsPlans.id == std::string{plan_id}))) {
    row.changes = std::string{record.changes};
    row.created_at = record.createdAt;
    row.expires_at = record.expiresAt;
    row.is_applied = record.appliedAt.has_value();
    row.is_found = true;
  }
  return row;
}

auto check_applicable(const PlanRow& plan, std::string_view plan_id) -> Result<void> {
  const auto id = std::string{plan_id};
  if (!plan.is_found) {
    return std::unexpected(Error::make(ErrorCode::not_found, "no plan " + id));
  }
  if (plan.is_applied) {
    return std::unexpected(Error::make(ErrorCode::conflict, "plan " + id + " was already applied"));
  }
  if (plan.expires_at <= now_stamp()) {
    return std::unexpected(Error::make(ErrorCode::conflict,
                                       "plan " + id + " expired at " +
                                           cms::db::format_timestamp(plan.expires_at) +
                                           "; plan the change again"));
  }
  return {};
}

/// Refuses a plan whose diff no longer describes the table: the value the plan names is already stored, or
/// the key moved after the plan was made. Either way the change that lands would not be the one the
/// operator read, and a plan is a decision only while its diff is still true. The no-op is checked first
/// because it is the more precise answer: a plan with nothing left to do stays a no-op however the key got
/// there.
auto check_still_wanted(db::connection& database, const PlanRow& plan,
                        const PlanChanges& changes) -> Result<void> {
  const auto stored = stored_value(database, changes.key);
  if (stored.is_present && stored.text == changes.value) {
    return std::unexpected(Error::make(ErrorCode::conflict,
                                       changes.key + " already holds the planned value; nothing to "
                                                     "apply"));
  }
  if (written_since(database, changes.key, plan.created_at)) {
    return std::unexpected(Error::make(ErrorCode::conflict,
                                       changes.key + " changed after this plan was made; plan it "
                                                     "again"));
  }
  return {};
}

auto apply_inside(db::connection& database, const std::vector<CatalogueEntry>& catalogue,
                  std::string_view plan_id, std::string_view actor_id) -> Result<AppliedPlan> {
  const auto plan = find_plan(database, plan_id);
  const auto applicable = check_applicable(plan, plan_id);
  if (!applicable) {
    return std::unexpected(applicable.error());
  }
  const auto changes = parse_changes(plan.changes);
  if (!changes) {
    return std::unexpected(changes.error());
  }
  const auto* entry = entry_for(catalogue, changes->key);
  if (entry == nullptr) {
    return std::unexpected(Error::make(ErrorCode::invalid_argument,
                                       "\"" + changes->key + "\" is not a setting this build "
                                                              "knows"));
  }
  if (!matches_kind(entry->kind, changes->value)) {
    return std::unexpected(
        Error::make(ErrorCode::invalid_argument, changes->key + " expects a " + entry->kind));
  }
  const auto wanted = check_still_wanted(database, plan, *changes);
  if (!wanted) {
    return std::unexpected(wanted.error());
  }

  const auto written =
      apply_settings(database, catalogue,
                     {SettingChange{.key = changes->key, .value = changes->value}}, actor_id);
  if (!written) {
    return std::unexpected(written.error());
  }
  const auto id = std::string{plan_id};
  database(sqlpp::update(db::settingsPlans)
               .set(db::settingsPlans.appliedAt = now_stamp())
               .where(db::settingsPlans.id == id && db::settingsPlans.appliedAt.is_null()));
  return AppliedPlan{.id = id, .key = changes->key};
}

}  // namespace

auto plan_setting(db::connection& database, const std::vector<CatalogueEntry>& catalogue,
                  std::string_view key, std::string_view value, std::string_view author_id)
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
  if (!has_uuid_shape(author_id)) {
    return std::unexpected(Error::make(ErrorCode::invalid_argument, "a plan needs the author's id"));
  }

  const auto stored = stored_value(database, key);
  const auto id = uuid_v4();
  auto expires_at = cms::db::timestamp{};
  try {
    database(sqlpp::insert_into(db::settingsPlans).set(
        db::settingsPlans.id = id, db::settingsPlans.authorId = std::string{author_id},
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
                std::string_view actor_id) -> Result<AppliedPlan> {
  if (!has_uuid_shape(plan_id)) {
    return std::unexpected(Error::make(ErrorCode::not_found, "no plan " + std::string{plan_id}));
  }
  if (!has_uuid_shape(actor_id)) {
    return std::unexpected(Error::make(ErrorCode::invalid_argument, "an apply needs the actor's id"));
  }
  try {
    auto transaction = cms::db::Transaction{*config->client};
    const auto applied = apply_inside(transaction.connection(), catalogue, plan_id, actor_id);
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
