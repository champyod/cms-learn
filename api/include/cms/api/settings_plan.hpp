#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <cms/api/settings_view.hpp>
#include <cms/db/connection.hpp>
#include <cms/error.hpp>

namespace cms::api {

/// @brief The diff a plan would make, as an operator has to see it before it lands.
struct PlanPreview {
  std::string id;
  std::string key;
  /// The stored value, or the catalogue default while no row holds one — and `REDACTED` for a secret,
  /// which is never read back on this path either.
  std::string old_value;
  std::string new_value;
  std::string tier;
  /// `hot` or `restart`, from the catalogue: whether the service picks the value up without a restart.
  std::string reload;
  std::string expires_at;
};

/// @brief Records a plan for one setting change and reports the diff it would make.
///
/// @param database The connection; the caller owns the transaction.
/// @param catalogue The keys this build understands.
/// @param key The setting to change.
/// @param value The value it would take.
/// @param author_id The principal the plan is attributed to.
/// @return The plan's id and diff, or the first fact that was refused.
///
/// Nothing in `settings` is written here: the plan exists so an operator can read the diff first, and
/// what turns it into a change is apply_plan(). The key and value are checked against the catalogue
/// before the row exists, so a plan that could never be applied is not recorded at all.
[[nodiscard]] Result<PlanPreview> plan_setting(db::connection& database,
                                               const std::vector<CatalogueEntry>& catalogue,
                                               std::string_view key, std::string_view value,
                                               std::string_view author_id);

/// @brief What an applied plan changed.
struct AppliedPlan {
  std::string id;
  std::string key;
};

/// @brief Applies exactly the plan with @p plan_id, once, in one transaction.
///
/// @param config The connection the service was started with.
/// @param catalogue The keys this build understands.
/// @param plan_id The plan to apply.
/// @param actor_id The principal the change is attributed to.
/// @return What was applied, or the reason this plan cannot be applied now.
///
/// A plan is refused when it is missing, already applied, expired, when its key left the catalogue, when
/// the key moved after the plan was made, and when the value it names is already stored. The last two are
/// what make a plan a decision rather than a delayed command: the diff the operator read has to still be
/// the diff that lands.
[[nodiscard]] Result<AppliedPlan> apply_plan(
    const std::shared_ptr<const db::connection_config>& config,
    const std::vector<CatalogueEntry>& catalogue, std::string_view plan_id,
    std::string_view actor_id);

}  // namespace cms::api
