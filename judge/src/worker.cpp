#include <cms/judge/worker.hpp>

#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sqlpp23/sqlpp23.h>

#include <cms/db/connection.hpp>
#include <cms/db/tables.hpp>

namespace cms::judge {
namespace {

// The lease is an interval rather than a column, so it travels as the statement's second parameter and one
// constant, CLAIM_LEASE, decides how long an abandoned claim lasts.
//
// `SKIP LOCKED` is the whole concurrency story: a row another worker is claiming at this instant is not a
// row this one waits for, and the subquery's row lock holds until the update that names it has set the
// claim, so the row cannot be handed out twice.
constexpr std::string_view CLAIM_SQL = R"claim(
UPDATE submissions SET claimed_by = $1, claimed_at = now()
WHERE id = (SELECT id FROM submissions
            WHERE verdict IS NULL
              AND (claimed_at IS NULL OR claimed_at < now() - make_interval(mins => $2::int))
            ORDER BY id
            LIMIT 1
            FOR UPDATE SKIP LOCKED)
RETURNING id, account_id, task_id, task_version, source
)claim";

constexpr std::string_view RELEASE_SQL = R"release(
UPDATE submissions SET claimed_by = NULL, claimed_at = NULL
WHERE verdict IS NULL
  AND claimed_at < now() - make_interval(mins => $1::int)
)release";

constexpr std::string_view FINISH_SQL = R"finish(
UPDATE submissions
SET verdict = $1::verdict_status, verdict_detail = $2, judged_at = now(),
    claimed_by = NULL, claimed_at = NULL
WHERE id = $3 AND verdict IS NULL
)finish";

// The RETURNING list read as a row, in the order the statement returns it. A result row names its members
// through a name tag, and the column's tag type is the one the generated header already declares. The uuid
// columns travel as text, so they decode as a string_view into the driver's row and are copied out before
// that row goes away.
using claim_id_tag = ::sqlpp::name_tag_of_t<cms::db::Submissions_::Id>;
using claim_account_id_tag = ::sqlpp::name_tag_of_t<cms::db::Submissions_::AccountId>;
using claim_task_id_tag = ::sqlpp::name_tag_of_t<cms::db::Submissions_::TaskId>;
using claim_task_version_tag = ::sqlpp::name_tag_of_t<cms::db::Submissions_::TaskVersion>;
using claim_source_tag = ::sqlpp::name_tag_of_t<cms::db::Submissions_::Source>;

using claim_row =
    ::sqlpp::result_row_t<::sqlpp::field_spec_t<claim_id_tag, ::sqlpp::text>,
                         ::sqlpp::field_spec_t<claim_account_id_tag, ::sqlpp::text>,
                         ::sqlpp::field_spec_t<claim_task_id_tag, ::sqlpp::text>,
                         ::sqlpp::field_spec_t<claim_task_version_tag, ::sqlpp::integral>,
                         ::sqlpp::field_spec_t<claim_source_tag, ::sqlpp::text>>;

auto claimed_from(const claim_row& row) -> ClaimedSubmission {
  return ClaimedSubmission{.id = std::string{row.id},
                           .account_id = std::string{row.accountId},
                           .task_id = std::string{row.taskId},
                           .task_version = row.taskVersion,
                           .source = std::string{row.source}};
}

}  // namespace

auto claim_submission(cms::db::connection& database, std::string_view worker_id)
    -> Result<std::optional<ClaimedSubmission>> {
  try {
    auto rows = ::sqlpp::result_t<cms::db::result, claim_row>{
        database.query_text(std::string{CLAIM_SQL},
                            {std::string{worker_id}, std::int64_t{CLAIM_LEASE.count()}})};
    if (rows.empty()) {
      return std::optional<ClaimedSubmission>{};
    }
    return std::optional<ClaimedSubmission>{claimed_from(rows.front())};
  } catch (const std::exception& failure) {
    return std::unexpected(Error::make(ErrorCode::unavailable, failure.what()));
  }
}

auto release_stale_claims(cms::db::connection& database, std::chrono::minutes age)
    -> Result<std::size_t> {
  try {
    return database.execute_text(std::string{RELEASE_SQL}, {std::int64_t{age.count()}});
  } catch (const std::exception& failure) {
    return std::unexpected(Error::make(ErrorCode::unavailable, failure.what()));
  }
}

auto finish_submission(cms::db::connection& database, std::string_view submission_id,
                       const Judgement& judgement) -> Result<void> {
  try {
    const auto written = database.execute_text(
        std::string{FINISH_SQL},
        {std::string{database_verdict(judgement.verdict)}, judgement.detail,
         std::string{submission_id}});
    if (written != 1) {
      return std::unexpected(Error::make(
          ErrorCode::conflict, "submission " + std::string{submission_id} +
                                   " is no longer an unjudged claimed row"));
    }
    return {};
  } catch (const std::exception& failure) {
    return std::unexpected(Error::make(ErrorCode::unavailable, failure.what()));
  }
}

}  // namespace cms::judge
