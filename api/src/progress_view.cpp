#include <cms/api/progress_view.hpp>

#include <exception>
#include <string>
#include <string_view>

#include <sqlpp23/sqlpp23.h>

#include <cms/db/connection.hpp>
#include <cms/db/tables.hpp>

namespace cms::api {
namespace {

/// The totals row: two counts and the two days the newest of each happened.
SQLPP_CREATE_NAME_TAG(attempted_count);
SQLPP_CREATE_NAME_TAG(solved_count);
SQLPP_CREATE_NAME_TAG(last_attempted);
SQLPP_CREATE_NAME_TAG(last_solved);

/// A topic row: its labels and the two counts that belong to it.
SQLPP_CREATE_NAME_TAG(topic_id);
SQLPP_CREATE_NAME_TAG(topic_label_en);
SQLPP_CREATE_NAME_TAG(topic_label_th);
SQLPP_CREATE_NAME_TAG(topic_attempted);
SQLPP_CREATE_NAME_TAG(topic_solved);

using totals_row = ::sqlpp::result_row_t<::sqlpp::field_spec_t<attempted_count_t::_sqlpp_name_tag, ::sqlpp::integral>,
                                         ::sqlpp::field_spec_t<solved_count_t::_sqlpp_name_tag, ::sqlpp::integral>,
                                         ::sqlpp::field_spec_t<last_attempted_t::_sqlpp_name_tag, ::sqlpp::text>,
                                         ::sqlpp::field_spec_t<last_solved_t::_sqlpp_name_tag, ::sqlpp::text>>;

using topic_row = ::sqlpp::result_row_t<::sqlpp::field_spec_t<topic_id_t::_sqlpp_name_tag, ::sqlpp::text>,
                                        ::sqlpp::field_spec_t<topic_label_en_t::_sqlpp_name_tag, ::sqlpp::text>,
                                        ::sqlpp::field_spec_t<topic_label_th_t::_sqlpp_name_tag, ::sqlpp::text>,
                                        ::sqlpp::field_spec_t<topic_attempted_t::_sqlpp_name_tag, ::sqlpp::integral>,
                                        ::sqlpp::field_spec_t<topic_solved_t::_sqlpp_name_tag, ::sqlpp::integral>>;

/// The learner's own rows, counted by what the table says about them. `attempted` and `solved` are exclusive
/// because a row is one or the other, so the page never adds a solved task to the attempt count.
constexpr std::string_view TOTALS_SQL = R"(
SELECT count(*) FILTER (WHERE status = 'attempted') AS attempted_count,
       count(*) FILTER (WHERE status = 'solved') AS solved_count,
       coalesce(to_char(max(attempted_at), 'YYYY-MM-DD'), '') AS last_attempted,
       coalesce(to_char(max(solved_at), 'YYYY-MM-DD'), '') AS last_solved
FROM progress
WHERE account_id = $1::uuid
)";

/// Every topic in the vocabulary, joined to this learner's rows through the tasks that carry it. A topic
/// with no row is a zero, not a missing entry: the page has to be able to say "untried" about it.
constexpr std::string_view TOPICS_SQL = R"(
SELECT t.id AS topic_id, t.label_en AS topic_label_en, t.label_th AS topic_label_th,
       count(p.id) FILTER (WHERE p.status = 'attempted') AS topic_attempted,
       count(p.id) FILTER (WHERE p.status = 'solved') AS topic_solved
FROM topics t
LEFT JOIN task_topics tt ON tt.topic = t.id
LEFT JOIN progress p ON p.account_id = $1::uuid
                    AND p.task_id = tt.task_id
                    AND p.task_version = tt.task_version
GROUP BY t.id, t.label_en, t.label_th
ORDER BY t.id
)";

}  // namespace

auto read_progress(db::connection& database, std::string_view learner_id) -> Result<ProgressView> {
  auto view = ProgressView{};
  try {
    auto totals = ::sqlpp::result_t<db::result, totals_row>{
        database.query_text(std::string{TOTALS_SQL}, {std::string{learner_id}})};
    if (!totals.empty()) {
      view.attempted = totals.front().attempted_count;
      view.solved = totals.front().solved_count;
      view.last_attempted_on = std::string{totals.front().last_attempted};
      view.last_solved_on = std::string{totals.front().last_solved};
    }

    auto rows = ::sqlpp::result_t<db::result, topic_row>{
        database.query_text(std::string{TOPICS_SQL}, {std::string{learner_id}})};
    for (const auto& row : rows) {
      view.topics.push_back(TopicProgress{.id = std::string{row.topic_id},
                                          .label_en = std::string{row.topic_label_en},
                                          .label_th = std::string{row.topic_label_th},
                                          .attempted = row.topic_attempted,
                                          .solved = row.topic_solved});
    }
  } catch (const std::exception& failure) {
    return std::unexpected(Error::make(ErrorCode::unavailable, failure.what()));
  }
  return view;
}

}  // namespace cms::api
