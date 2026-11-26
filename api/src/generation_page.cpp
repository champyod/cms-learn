#include <cms/api/generation_page.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <sqlpp23/sqlpp23.h>

#include <cms/api/web.hpp>
#include <cms/db/connection.hpp>

namespace cms::api {
namespace {

SQLPP_CREATE_NAME_TAG(job_id);
SQLPP_CREATE_NAME_TAG(task_id);
SQLPP_CREATE_NAME_TAG(state);
SQLPP_CREATE_NAME_TAG(steps);
SQLPP_CREATE_NAME_TAG(refusal);

using job_row = ::sqlpp::result_row_t<::sqlpp::field_spec_t<job_id_t::_sqlpp_name_tag, ::sqlpp::integral>,
                                      ::sqlpp::field_spec_t<task_id_t::_sqlpp_name_tag, ::sqlpp::text>,
                                      ::sqlpp::field_spec_t<state_t::_sqlpp_name_tag, ::sqlpp::text>>;
using steps_row = ::sqlpp::result_row_t<::sqlpp::field_spec_t<job_id_t::_sqlpp_name_tag, ::sqlpp::integral>,
                                        ::sqlpp::field_spec_t<steps_t::_sqlpp_name_tag, ::sqlpp::text>>;
using refusal_row = ::sqlpp::result_row_t<::sqlpp::field_spec_t<job_id_t::_sqlpp_name_tag, ::sqlpp::integral>,
                                          ::sqlpp::field_spec_t<refusal_t::_sqlpp_name_tag, ::sqlpp::text>>;

constexpr std::string_view RUNNING = "running";
constexpr std::string_view FAILED = "failed";
constexpr std::string_view REFUSAL_KIND = "refusal";

constexpr std::string_view JOBS_SQL = R"(
SELECT id AS job_id, task_id, state FROM generation_jobs ORDER BY id DESC LIMIT $1
)";

/// Every step that wrote an artefact, per job, as a comma-separated list the caller parses. The ids are
/// integers this function read from the database, so they are interpolated rather than bound: the parameter
/// type carries one value, and an array parameter is not one.
constexpr std::string_view STEPS_SQL_HEAD = R"(
SELECT job_id, array_to_string(array_agg(DISTINCT step), ',') AS steps
FROM generation_artefacts
WHERE job_id IN ()";

/// The refusal a failed job holds. A step writes one before it ends the claim, so the newest is the reason.
constexpr std::string_view REFUSALS_SQL_HEAD = R"(
SELECT DISTINCT ON (job_id) job_id, coalesce(inline_text, '') AS refusal
FROM generation_artefacts
WHERE job_id IN ()";

auto steps_of(std::string_view csv) -> std::set<int> {
  auto parsed = std::set<int>{};
  auto start = std::size_t{0};
  while (start < csv.size()) {
    const auto comma = csv.find(',', start);
    const auto part = csv.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
    if (!part.empty()) {
      parsed.insert(std::stoi(std::string{part}));
    }
    if (comma == std::string_view::npos) {
      break;
    }
    start = comma + 1;
  }
  return parsed;
}

/// Seven marks in one line: a number is done, a dot is the step the job is on, a dash is not reached.
auto steps_line_of(const std::set<int>& done, std::string_view job_state) -> std::string {
  auto lowest_missing = 0;
  for (auto step = 1; step <= 7; ++step) {
    if (done.count(step) == 0) {
      lowest_missing = step;
      break;
    }
  }
  auto line = std::string{};
  for (auto step = 1; step <= 7; ++step) {
    if (!line.empty()) {
      line.push_back(' ');
    }
    if (done.count(step) != 0) {
      line += std::to_string(step);
      continue;
    }
    line += (job_state == RUNNING && step == lowest_missing) ? "." : "-";
  }
  return line;
}

}  // namespace

auto read_generation_jobs(db::connection& database, std::size_t limit) -> std::vector<GenerationJobView> {
  auto jobs = std::vector<GenerationJobView>{};
  for (const auto& row : ::sqlpp::result_t<db::result, job_row>{database.query_text(
           std::string{JOBS_SQL}, {static_cast<std::int64_t>(limit)})}) {
    jobs.push_back(GenerationJobView{.id = std::to_string(row.job_id),
                                     .task_id = std::string{row.task_id},
                                     .state = std::string{row.state},
                                     .refusal = {},
                                     .steps_line = {},
                                     .steps_done = {},
                                     .has_refusal = false});
  }
  if (jobs.empty()) {
    return jobs;
  }
  auto id_list = std::string{};
  for (const auto& job : jobs) {
    if (!id_list.empty()) {
      id_list.push_back(',');
    }
    id_list += job.id;
  }
  auto done_by_job = std::map<std::string, std::set<int>>{};
  for (const auto& row : ::sqlpp::result_t<db::result, steps_row>{database.query_text(
           std::string{STEPS_SQL_HEAD} + id_list + ") AND kind <> 'refusal' GROUP BY job_id", {})}) {
    done_by_job[std::to_string(row.job_id)] = steps_of(std::string{row.steps});
  }
  for (const auto& row : ::sqlpp::result_t<db::result, refusal_row>{database.query_text(
           std::string{REFUSALS_SQL_HEAD} + id_list + ") AND kind = 'refusal' ORDER BY job_id, id DESC", {})}) {
    for (auto& job : jobs) {
      if (job.id == std::to_string(row.job_id)) {
        job.refusal = std::string{row.refusal};
        job.has_refusal = !row.refusal.empty();
      }
    }
  }
  for (auto& job : jobs) {
    const auto& done = done_by_job[job.id];
    job.steps_line = steps_line_of(done, job.state);
    job.steps_done = std::to_string(done.size()) + "/7";
  }
  return jobs;
}

auto read_generation_job(db::connection& database, std::string_view job_id) -> std::optional<GenerationJobView> {
  const auto jobs = read_generation_jobs(database, 200);
  const auto found = std::find_if(jobs.begin(), jobs.end(), [job_id](const auto& job) { return job.id == job_id; });
  if (found == jobs.end()) {
    return std::nullopt;
  }
  return *found;
}

auto generation_stream_payload(const GenerationJobView& job) -> std::string {
  auto payload = std::string{"event: state\n"};
  const auto body = nlohmann::json{{"id", job.id},
                                   {"state", job.state},
                                   {"steps_done", job.steps_done},
                                   {"steps_line", job.steps_line},
                                   {"refusal", job.refusal},
                                   {"has_refusal", job.has_refusal}}
                        .dump();
  payload += "data: " + body + "\n\n";
  return payload;
}

auto generation_model(const Locale& locale, std::string_view site_name, const std::vector<GenerationJobView>& jobs,
                      std::string_view csrf_token) -> nlohmann::json {
  auto data = shell_model(locale, site_name, LevelView{});
  data["heading"] = "สร้างโจทย์";
  data["lede"] = "งานที่สั่งไว้ และขั้นตอนที่เดินไปแล้ว อ่านจากการบันทึกของแต่ละขั้น ไม่ใช่จากการประมาณ";
  data["new_heading"] = "สั่งสร้างงานใหม่";
  data["task_id_label"] = "รหัสโจทย์";
  data["task_version_label"] = "เวอร์ชัน";
  data["brief_label"] = "สิ่งที่ต้องการ";
  data["submit_label"] = "สั่งสร้าง";
  data["jobs_heading"] = "งานที่สั่งไว้";
  data["empty_label"] = "ยังไม่มีงาน";
  data["refusal_label"] = "เหตุผลที่หยุด";
  data["action"] = "/admin/generate";
  data["csrf_token"] = std::string{csrf_token};
  data["has_jobs"] = !jobs.empty();
  data["jobs"] = nlohmann::json::array();
  for (const auto& job : jobs) {
    data["jobs"].push_back({{"id", job.id},
                            {"task_id", job.task_id},
                            {"state", job.state},
                            {"refusal", job.refusal},
                            {"has_refusal", job.has_refusal},
                            {"steps_line", job.steps_line},
                            {"steps_done", job.steps_done}});
  }
  return data;
}

}  // namespace cms::api
