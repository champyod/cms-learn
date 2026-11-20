#include <cms/generate/pipeline.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <sqlpp23/sqlpp23.h>

#include <cms/db/connection.hpp>
#include <cms/db/tables.hpp>
#include <cms/generate/planner.hpp>
#include <cms/generate/sampler.hpp>

namespace cms::generate {
namespace {

constexpr std::int16_t PLAN_STEP = 1;
constexpr std::int16_t SPECIFICATION_STEP = 3;
constexpr std::string_view REFUSAL_KIND = "refusal";
constexpr std::string_view AWAITING_EVIDENCE = "awaiting-evidence";
constexpr std::string_view FAILED = "failed";

auto refused(std::string_view message) -> Error {
  return Error::make(ErrorCode::invalid_argument, message);
}

}  // namespace

auto load_brief(cms::db::connection& database, const JobClaim& claim) -> Result<Brief> {
  auto brief = Brief{};
  auto found = false;
  for (const auto& row : database(sqlpp::select(cms::db::generationJobs.brief, cms::db::generationJobs.description)
                                      .from(cms::db::generationJobs)
                                      .where(cms::db::generationJobs.id == claim.id))) {
    brief.brief = std::string{row.brief};
    brief.description = std::string{row.description};
    found = true;
  }
  if (!found) {
    return std::unexpected(refused("generation job " + std::to_string(claim.id) + " does not exist"));
  }

  for (const auto& row : database(sqlpp::select(cms::db::tasks.difficulty)
                                      .from(cms::db::tasks)
                                      .where(cms::db::tasks.id == claim.task_id &&
                                             cms::db::tasks.version == static_cast<std::int64_t>(claim.task_version)))) {
    brief.difficulty = std::to_string(row.difficulty);
  }

  for (const auto& row : database(sqlpp::select(cms::db::taskTopics.topic)
                                      .from(cms::db::taskTopics)
                                      .where(cms::db::taskTopics.taskId == claim.task_id &&
                                             cms::db::taskTopics.taskVersion ==
                                                 static_cast<std::int64_t>(claim.task_version))
                                      .order_by(cms::db::taskTopics.topic.asc()))) {
    brief.topics.emplace_back(row.topic);
  }
  if (brief.topics.empty()) {
    return std::unexpected(refused("generation job " + std::to_string(claim.id) + ": task " + claim.task_id +
                                   " version " + std::to_string(claim.task_version) +
                                   " declares no topic, so the brief cannot name the technique it teaches"));
  }
  brief.technique = brief.topics.front();

  for (const auto& row : database(sqlpp::select(cms::db::taskStatements.locale)
                                      .from(cms::db::taskStatements)
                                      .where(cms::db::taskStatements.taskId == claim.task_id &&
                                             cms::db::taskStatements.taskVersion ==
                                                 static_cast<std::int64_t>(claim.task_version))
                                      .order_by(cms::db::taskStatements.locale.asc()))) {
    const auto locale = std::string{row.locale};
    if (std::find(brief.locales.begin(), brief.locales.end(), locale) == brief.locales.end()) {
      brief.locales.emplace_back(locale);
    }
  }
  return brief;
}

auto run_pipeline(ModelClient& client, cms::db::connection& database, std::string_view worker,
                  CandidateSink& candidates, SpecificationSink& specifications, ArtefactSink& artefacts)
    -> Result<PipelineReport> {
  const auto claimed = claim_job(database, worker);
  if (!claimed) {
    return std::unexpected(claimed.error());
  }
  if (!claimed->has_value()) {
    return PipelineReport{};
  }
  const auto& claim = **claimed;
  auto report = PipelineReport{.job_id = claim.id, .state = {}, .steps = {}};

  // A refusal ends the attempt, never leaves the job running: the step's words go into the record as one inline
  // artefact row and the claim ends failed, so a reader of the job sees why without reading a log.
  const auto refuse = [&](std::int16_t step, std::string_view step_name, const Error& error) -> Error {
    const auto artefact = Artefact{.step = step,
                                   .kind = std::string{REFUSAL_KIND},
                                   .locale = {},
                                   .home = ArtefactHome::row,
                                   .object_key = {},
                                   .bytes = error.message,
                                   .call = ModelCall{}};
    const auto recorded = artefacts.record(claim.id, {artefact});
    const auto finished = finish_claim(database, claim.id, worker, FAILED);
    auto message = "step " + std::to_string(step) + " (" + std::string{step_name} + "): " + error.message;
    if (!recorded) {
      message += "; the refusal could not be recorded: " + recorded.error().message;
    }
    if (!finished) {
      message += "; the claim could not be ended: " + finished.error().message;
    }
    return Error::make(error.code, message);
  };

  const auto brief = load_brief(database, claim);
  if (!brief) {
    return std::unexpected(refuse(PLAN_STEP, "the brief", brief.error()));
  }
  const auto planned = plan_candidates(client, *brief);
  if (!planned) {
    return std::unexpected(refuse(PLAN_STEP, "plan", planned.error()));
  }
  report.steps.emplace_back("plan (" + std::to_string(planned->size()) + " candidate(s))");

  const auto judged = judge_candidates(client, *planned);
  if (!judged) {
    return std::unexpected(refuse(PLAN_STEP + 1, "judge", judged.error()));
  }
  const auto recorded_candidates = candidates.record(claim.id, *judged);
  if (!recorded_candidates) {
    return std::unexpected(refuse(PLAN_STEP + 1, "judge", recorded_candidates.error()));
  }
  report.steps.emplace_back("judge");

  const auto answer = specify_step3(client, chosen_of(*judged).plan);
  if (!answer) {
    return std::unexpected(refuse(SPECIFICATION_STEP, "specification", answer.error()));
  }
  const auto smallest = smallest_case(answer->specification.bounds);
  if (!smallest) {
    return std::unexpected(refuse(SPECIFICATION_STEP, "specification", smallest.error()));
  }
  const auto checked = check_oracle(answer->oracle_source, smallest->input, answer->limit.time_limit_ms);
  if (!checked) {
    return std::unexpected(refuse(SPECIFICATION_STEP, "specification", checked.error()));
  }
  const auto recorded_specification = specifications.record(claim.id, *answer, *checked);
  if (!recorded_specification) {
    return std::unexpected(refuse(SPECIFICATION_STEP, "specification", recorded_specification.error()));
  }
  report.steps.emplace_back("specification");

  const auto finished = finish_claim(database, claim.id, worker, AWAITING_EVIDENCE);
  if (!finished) {
    return std::unexpected(finished.error());
  }
  report.state = std::string{AWAITING_EVIDENCE};
  return report;
}

}  // namespace cms::generate
