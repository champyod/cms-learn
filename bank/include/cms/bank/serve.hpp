#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <cms/bank/bank.hpp>
#include <cms/db/connection.hpp>
#include <cms/error.hpp>

namespace cms::bank {

/// @brief What a learner is given: one published task, and whether the stage's band had to stretch.
struct NextTask {
  std::string id;
  std::int32_t version{0};
  /// True when nothing in the stage's own band was left unseen and the band was widened once.
  bool is_widened{false};
};

/// @brief The next task an account has not touched, chosen from a stage's tag set and band.
///
/// @param database The connection to read through.
/// @param account_id The learner.
/// @param stage The route stage: its tags select, its band filters.
/// @return The task, or nothing when even the widened band holds nothing unseen.
///
/// The band widens exactly once, by one step at each end, because a stage whose every task is done should
/// hand out a neighbouring task rather than nothing — and should not wander arbitrarily far to do it. The
/// order is deterministic so two accounts asking the same question get the same answer.
[[nodiscard]] Result<std::optional<NextTask>> next_unseen_task(db::connection& database,
                                                              std::string_view account_id,
                                                              const Stage& stage);

}  // namespace cms::bank
