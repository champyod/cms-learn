#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <cms/api/web.hpp>
#include <cms/db/connection.hpp>

namespace cms::api {

/// @brief One job as the screen shows it: what was asked for, how far it got, and how it stopped.
struct GenerationJobView {
  std::string id;
  std::string task_id;
  std::string state;
  std::string refusal;
  std::string steps_line;
  std::string steps_done;
  bool has_refusal{false};
};

/// @brief Reads the most recent jobs, with the steps their artefacts prove and the refusal they hold.
///
/// Nothing here writes: a job's progress is the set of steps `generation_artefacts` holds for it, and its
/// refusal is the artefact of kind `refusal` that the step wrote before it ended the claim (ADR 0062 §3).
[[nodiscard]] auto read_generation_jobs(db::connection& database, std::size_t limit) -> std::vector<GenerationJobView>;

/// @brief One job by id, or nothing when it is not there: the stream reads this once per connection.
[[nodiscard]] auto read_generation_job(db::connection& database, std::string_view job_id)
    -> std::optional<GenerationJobView>;

/// @brief The server-sent event that carries one job's progress, as the page's script reads it.
[[nodiscard]] auto generation_stream_payload(const GenerationJobView& job) -> std::string;

/// @brief The template model for the generation screen.
[[nodiscard]] auto generation_model(const Locale& locale, std::string_view site_name,
                                    const std::vector<GenerationJobView>& jobs, std::string_view csrf_token)
    -> nlohmann::json;

}  // namespace cms::api
