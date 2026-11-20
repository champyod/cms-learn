#include <cms/bank/serve.hpp>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <sqlpp23/sqlpp23.h>

#include <cms/db/tables.hpp>

namespace cms::bank {
namespace {

/// The count's column name, so a result row can read it back without repeating the expression.
SQLPP_CREATE_NAME_TAG(row_count);

constexpr std::int32_t LOWEST_DIFFICULTY = 800;
constexpr std::int32_t HIGHEST_DIFFICULTY = 3500;
constexpr std::int32_t WIDENING_STEP = 200;

/// The one rule that decides what a learner may see: a task carries a grant for one of the audience's encoded
/// values. The serving query and the count both read this definition, because the count is what lets a stage say it
/// has nothing left — and a rule written twice would let the two answers drift apart while one of them serves.
auto is_visible_to(const std::vector<std::int64_t>& principals) {
  return sqlpp::exists(
      sqlpp::select(db::taskVisibility.taskId)
          .from(db::taskVisibility)
          .where(db::taskVisibility.taskId == db::tasks.id &&
                 db::taskVisibility.principalId.in(principals)));
}

/// One candidate: published, visible to one of the audience's principals, carries a topic of the stage,
/// inside the band, and not yet attempted by the account. Written as anti-joins so the planner can use
/// progress's index for the last one.
auto find_in_band(db::connection& database, std::string_view account_id, const Stage& stage,
                  std::span<const std::int64_t> audience, std::int32_t minimum,
                  std::int32_t maximum) -> std::optional<NextTask> {
  const auto principals = std::vector<std::int64_t>(audience.begin(), audience.end());

  const auto is_visible = is_visible_to(principals);

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
                    // Both facts, not one: a row that is published but not verified is a row no machine has certified,
                    // and 0057 makes that combination unservable even if a future writer creates it.
                    .where(db::tasks.status == std::string{"published"} &&
                           db::tasks.verification == std::string{"verified"} &&
                           db::tasks.difficulty >= minimum && db::tasks.difficulty <= maximum &&
                           is_visible && carries_stage_topic && !is_attempted)
                    .order_by(db::tasks.difficulty.asc(), db::tasks.id.asc())
                    .limit(1))) {
    return NextTask{.id = std::string{row.id},
                    .version = static_cast<std::int32_t>(row.version),
                    .is_widened = false};
  }
  return std::nullopt;
}

/// The stage's tasks the account may see, counted once. The visibility rule is `is_visible_to`'s definition, the
/// same one `find_in_band` reads, so the count and the offer cannot answer different questions.
auto count_stage_tasks(db::connection& database, const Stage& stage,
                       std::span<const std::int64_t> audience,
                       const std::optional<std::string_view>& solved_by) -> std::int64_t {
  const auto principals = std::vector<std::int64_t>(audience.begin(), audience.end());
  const auto is_visible = is_visible_to(principals);
  const auto carries_stage_topic = sqlpp::exists(
      sqlpp::select(db::taskTopics.taskId)
          .from(db::taskTopics)
          .where(db::taskTopics.taskId == db::tasks.id &&
                 db::taskTopics.taskVersion == db::tasks.version &&
                 db::taskTopics.topic.in(stage.tags)));
  const auto one = sqlpp::value(1);

  auto count = std::int64_t{0};
  if (solved_by) {
    const auto is_solved = sqlpp::exists(
        sqlpp::select(db::progress.id)
            .from(db::progress)
            .where(db::progress.accountId == std::string{*solved_by} &&
                   db::progress.taskId == db::tasks.id &&
                   db::progress.taskVersion == db::tasks.version &&
                   db::progress.status == std::string{"solved"}));
    for (const auto& row : database(sqlpp::select(sqlpp::count(one).as(row_count))
                                        .from(db::tasks)
                                        .where(db::tasks.status == std::string{"published"} &&
                                               db::tasks.difficulty >= stage.difficulty_min &&
                                               db::tasks.difficulty <= stage.difficulty_max &&
                                               is_visible && carries_stage_topic && is_solved))) {
      count = static_cast<std::int64_t>(row.row_count);
    }
    return count;
  }
  for (const auto& row : database(sqlpp::select(sqlpp::count(one).as(row_count))
                                      .from(db::tasks)
                                      .where(db::tasks.status == std::string{"published"} &&
                                             db::tasks.difficulty >= stage.difficulty_min &&
                                             db::tasks.difficulty <= stage.difficulty_max &&
                                             is_visible && carries_stage_topic))) {
    count = static_cast<std::int64_t>(row.row_count);
  }
  return count;
}

auto widen(std::int32_t minimum, std::int32_t maximum) -> std::pair<std::int32_t, std::int32_t> {
  const auto lower = std::max(LOWEST_DIFFICULTY, minimum - WIDENING_STEP);
  const auto upper = std::min(HIGHEST_DIFFICULTY, maximum + WIDENING_STEP);
  return {lower, upper};
}

}  // namespace

auto next_unseen_task_in_band(db::connection& database, std::string_view account_id,
                             const Stage& stage, std::span<const std::int64_t> audience)
    -> Result<std::optional<NextTask>> {
  if (audience.empty()) {
    return std::unexpected(Error::make(
        ErrorCode::invalid_argument, "the audience is empty; every request carries the everyone entry"));
  }
  try {
    return find_in_band(database, account_id, stage, audience, stage.difficulty_min,
                        stage.difficulty_max);
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }
}

auto stage_progress(db::connection& database, std::string_view account_id, const Stage& stage,
                    std::span<const std::int64_t> audience) -> Result<StageProgress> {
  try {
    const auto total = count_stage_tasks(database, stage, audience, std::nullopt);
    const auto done = count_stage_tasks(database, stage, audience, account_id);
    return StageProgress{.total = total, .done = done};
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }
}

auto next_unseen_task(db::connection& database, std::string_view account_id, const Stage& stage,
                      std::span<const std::int64_t> audience)
    -> Result<std::optional<NextTask>> {
  if (audience.empty()) {
    return std::unexpected(Error::make(
        ErrorCode::invalid_argument, "the audience is empty; every request carries the everyone entry"));
  }
  try {
    const auto in_band = find_in_band(database, account_id, stage, audience, stage.difficulty_min,
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
    auto widened = find_in_band(database, account_id, stage, audience, lower, upper);
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
