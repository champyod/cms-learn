#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <cms/db/connection.hpp>
#include <cms/error.hpp>

namespace cms::api {

/// @brief One topic, as the progress page reads it: how much of it the learner has touched, and how much of
/// that was solved.
///
/// Both counts come from `progress` alone, so a number on the page cannot disagree with the table it names.
struct TopicProgress {
  std::string id;
  std::string label_en;
  std::string label_th;
  std::int64_t attempted{0};
  std::int64_t solved{0};
};

/// @brief What one learner has done, read from `progress` and nothing else.
///
/// `attempted` counts rows still marked attempted and `solved` counts rows marked solved; they are exclusive
/// on purpose, so their sum is the number of task versions the learner has touched — the one number the
/// table itself can support. The two day strings are when the newest attempt and the newest solve happened,
/// empty when there is none.
struct ProgressView {
  std::vector<TopicProgress> topics;
  std::int64_t attempted{0};
  std::int64_t solved{0};
  std::string last_attempted_on;
  std::string last_solved_on;
};

/// @brief Reads @p learner_id's own progress.
///
/// @param database The connection to read through.
/// @param learner_id The learner whose UUID names the rows; a request never passes anyone else's.
/// @return The view, or the reason the read failed.
///
/// A topic with no rows reads as zero and zero, which the page shows as untried: there is no threshold in
/// the schema, so nothing here can call a topic weak, and this function does not invent one.
[[nodiscard]] Result<ProgressView> read_progress(db::connection& database, std::string_view learner_id);

}  // namespace cms::api
