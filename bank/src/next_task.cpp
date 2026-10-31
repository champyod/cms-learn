#include <cms/bank/serve.hpp>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <vector>

#include <sqlpp23/sqlpp23.h>

#include <cms/db/tables.hpp>

namespace cms::bank {
namespace {

constexpr std::int32_t LOWEST_DIFFICULTY = 800;
constexpr std::int32_t HIGHEST_DIFFICULTY = 3500;
constexpr std::int32_t WIDENING_STEP = 200;

/// One candidate: published, carries a topic of the stage, inside the band, and not yet attempted by the
/// account. Written as two anti-joins so the planner can use progress's index for the second one.
auto find_in_band(db::connection& database, std::string_view account_id, const Stage& stage,
                  std::int32_t minimum, std::int32_t maximum) -> std::optional<NextTask> {
  const auto carries_stage_topic = sqlpp::exists(
      sqlpp::select(db::taskTopics.taskId)
          .from(db::taskTopics)
          .where(db::taskTopics.taskId == db::tasks.id &&
                 db::taskTopics.taskVersion == db::tasks.version &&
                 db::taskTopics.topic.in(stage.tags)));

  const auto is_attempted = sqlpp::exists(
      sqlpp::select(db::progress.taskId)
          .from(db::progress)
          .where(db::progress.accountId == std::string{account_id} &&
                 db::progress.taskId == db::tasks.id &&
                 db::progress.taskVersion == db::tasks.version));

  for (const auto& row :
       database(sqlpp::select(db::tasks.id, db::tasks.version)
                    .from(db::tasks)
                    .where(db::tasks.status == std::string{"published"} &&
                           db::tasks.difficulty >= minimum && db::tasks.difficulty <= maximum &&
                           carries_stage_topic && !is_attempted)
                    .order_by(db::tasks.difficulty.asc(), db::tasks.id.asc())
                    .limit(1))) {
    return NextTask{.id = std::string{row.id},
                    .version = static_cast<std::int32_t>(row.version),
                    .is_widened = false};
  }
  return std::nullopt;
}

auto widen(std::int32_t minimum, std::int32_t maximum) -> std::pair<std::int32_t, std::int32_t> {
  const auto lower = std::max(LOWEST_DIFFICULTY, minimum - WIDENING_STEP);
  const auto upper = std::min(HIGHEST_DIFFICULTY, maximum + WIDENING_STEP);
  return {lower, upper};
}

}  // namespace

auto next_unseen_task(db::connection& database, std::string_view account_id, const Stage& stage)
    -> Result<std::optional<NextTask>> {
  try {
    const auto in_band = find_in_band(database, account_id, stage, stage.difficulty_min,
                                      stage.difficulty_max);
    if (in_band) {
      return *in_band;
    }

    // One widening, and only when it actually reaches something new; a stage already at the floor and
    // ceiling would otherwise re-run the same query.
    const auto [lower, upper] = widen(stage.difficulty_min, stage.difficulty_max);
    if (lower == stage.difficulty_min && upper == stage.difficulty_max) {
      return std::optional<NextTask>{};
    }
    auto widened = find_in_band(database, account_id, stage, lower, upper);
    if (!widened) {
      return std::optional<NextTask>{};
    }
    widened->is_widened = true;
    return widened;
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }
}

}  // namespace cms::bank
