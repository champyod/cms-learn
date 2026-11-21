#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <sqlpp23/sqlpp23.h>

#include <cms/db/connection.hpp>
#include <cms/db/tables.hpp>

namespace cms::generate::test {

constexpr auto PROBE_THAI_TITLE = "ทดสอบ";

/// @brief Writes one draft task and one generation job for it, the way the schema owner would.
/// @return The job's id, which every artefact row hangs from.
[[nodiscard]] inline auto prepare_job(cms::db::connection& database, std::string_view task_id)
    -> std::int64_t {
  database.execute_text(
      "INSERT INTO tasks (id, version, status, title_en, title_th, difficulty, time_limit_ms, "
      "memory_used_limit_mb, memory_reserved_limit_mb, checker, origin, verification, solution, content_hash) "
      "VALUES ($1, 1, 'draft', 'Probe', $2, 900, 1000, 256, 512, 'token', 'original', 'pending', '', '')",
      {std::string{task_id}, std::string{PROBE_THAI_TITLE}});
  database.execute_text("INSERT INTO generation_jobs (task_id, task_version) VALUES ($1, 1)",
                        {std::string{task_id}});
  auto job = std::int64_t{0};
  for (const auto& row : database(sqlpp::select(cms::db::generationJobs.id)
                                      .from(cms::db::generationJobs)
                                      .where(cms::db::generationJobs.taskId == std::string{task_id})
                                      .order_by(cms::db::generationJobs.id.desc())
                                      .limit(1))) {
    job = row.id;
  }
  return job;
}

}  // namespace cms::generate::test
