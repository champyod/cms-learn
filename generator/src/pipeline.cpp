#include <cms/generate/pipeline.hpp>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <exception>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <simdjson.h>
#include <sqlpp23/sqlpp23.h>

#include <cms/db/connection.hpp>
#include <cms/db/tables.hpp>
#include <cms/db/transaction.hpp>
#include <cms/generate/planner.hpp>
#include "json.hpp"
#include <cms/generate/sampler.hpp>

namespace cms::generate {
namespace {

constexpr std::int16_t PLAN_STEP = 1;
constexpr std::int16_t JUDGE_STEP = 2;
constexpr std::int16_t SPECIFICATION_STEP = 3;
constexpr std::int16_t SOLUTION_STEP = 5;
constexpr std::int16_t TEST_PLAN_STEP = 6;
constexpr std::int16_t CERTIFICATE_STEP = 7;
constexpr std::string_view SPECIFICATION_KIND = "specification";
constexpr std::string_view ORACLE_KIND = "oracle";
constexpr std::string_view REFERENCE_KIND = "reference";
constexpr std::string_view REFUSAL_KIND = "refusal";
constexpr std::string_view AWAITING_EVIDENCE = "awaiting-evidence";
constexpr std::string_view DONE = "done";
constexpr std::string_view FAILED = "failed";

auto refused(std::string_view message) -> Error {
  return Error::make(ErrorCode::invalid_argument, message);
}

/// @brief The specification step 3 recorded, with the limits it derived.
struct SpecificationRecord {
  Specification specification;
  std::int64_t time_limit_ms{0};
  std::int64_t memory_used_limit_mb{0};
  std::int64_t memory_reserved_limit_mb{0};
};

/// @return The artefact of @p step and @p kind, read back through the digest its row records, or the refusal naming what
/// the next step cannot start without.
auto read_step_artefact(::drogon::orm::DbClient& client, const cms::store::ObjectStore& store,
                        std::int64_t job_id, std::int16_t step, std::string_view kind) -> Result<StoredArtefact> {
  const auto stored = read_artefact(client, store, job_id, step, kind);
  if (!stored) {
    return std::unexpected(refused("step " + std::to_string(step) + " (the step before " +
                                   std::to_string(step + 1) + ") recorded no " + std::string{kind} +
                                   ", so nothing may be recorded on top of it: " + stored.error().message));
  }
  return stored;
}

/// @brief Reads the specification and the oracle step 3 wrote, at the bytes their rows address, and the derived limits.
auto read_specification_record(::drogon::orm::DbClient& client, cms::db::connection& database,
                               const cms::store::ObjectStore& store, std::int64_t job_id)
    -> Result<SpecificationRecord> {
  const auto answer = read_step_artefact(client, store, job_id, SPECIFICATION_STEP, SPECIFICATION_KIND);
  if (!answer) {
    return std::unexpected(answer.error());
  }
  const auto specification = read_specification(answer->bytes);
  if (!specification) {
    return std::unexpected(specification.error());
  }
  auto record = SpecificationRecord{.specification = *specification};
  for (const auto& row : database(sqlpp::select(cms::db::generationSpecifications.timeLimitMs,
                                                cms::db::generationSpecifications.memoryUsedLimitMb,
                                                cms::db::generationSpecifications.memoryReservedLimitMb)
                                      .from(cms::db::generationSpecifications)
                                      .where(cms::db::generationSpecifications.jobId == job_id))) {
    record.time_limit_ms = static_cast<std::int64_t>(row.timeLimitMs);
    record.memory_used_limit_mb = static_cast<std::int64_t>(row.memoryUsedLimitMb);
    record.memory_reserved_limit_mb = static_cast<std::int64_t>(row.memoryReservedLimitMb);
  }
  if (record.time_limit_ms == 0) {
    return std::unexpected(refused("step 3 recorded no specification row for generation job " +
                                   std::to_string(job_id) + ", so no limit was derived"));
  }
  return record;
}

/// @brief The chosen candidate's four texts, which the brief check compares against.
struct ChosenTexts {
  PlanTexts texts;
  std::string plan_json;
};

auto read_chosen_texts(cms::db::connection& database, std::int64_t job_id) -> Result<ChosenTexts> {
  auto chosen = ChosenTexts{};
  auto found = false;
  for (const auto& row : database(sqlpp::select(cms::db::generationCandidates.approach,
                                                cms::db::generationCandidates.intendedComplexity,
                                                cms::db::generationCandidates.plan, cms::db::generationCandidates.reason)
                                      .from(cms::db::generationCandidates)
                                      .where(cms::db::generationCandidates.jobId == job_id &&
                                             cms::db::generationCandidates.chosen == true))) {
    chosen.texts.approach = std::string{row.approach};
    chosen.texts.intended_complexity = std::string{row.intendedComplexity};
    chosen.plan_json = std::string{row.plan};
    chosen.texts.plan = chosen.plan_json;
    chosen.texts.reason = std::string{row.reason};
    found = true;
  }
  if (!found) {
    return std::unexpected(refused("generation job " + std::to_string(job_id) +
                                   " records no chosen candidate, so step 4 has no plan to check against"));
  }
  return chosen;
}

/// @brief Moves a job out of awaiting-evidence, which is the ending the certificate decides.
///
/// The claim is already ended at that point — the certifier reads a job whose evidence is complete and no longer running —
/// so this is the record's own transition, not another attempt ending: the state machine allows awaiting-evidence to
/// done or failed, and nothing else.
auto mark_job(cms::db::connection& database, std::int64_t job_id, std::string_view state) -> Result<void> {
  try {
    database.execute_text(
        "UPDATE generation_jobs SET state = $2::generation_state, updated_at = now() WHERE id = $1",
        {job_id, std::string{state}});
  } catch (const std::exception& failure) {
    return std::unexpected(Error::make(ErrorCode::unavailable, failure.what()));
  }
  return {};
}

/// @return The subjects the two leak checks read: each locale's title and the markdown a student would see.
auto leak_subjects(const Step4Answer& answer) -> std::vector<LeakSubject> {
  auto subjects = std::vector<LeakSubject>{};
  for (const auto& statement : answer.statements) {
    subjects.push_back(LeakSubject{.locale = statement.locale,
                                   .title = statement.title,
                                   .statement = render_statement(statement, answer.example)});
  }
  return subjects;
}

}  // namespace

SqlCertifier::SqlCertifier(::drogon::orm::DbClient& client, const cms::store::ObjectStore& store,
                           std::string_view commit_id)
    : client_(client), store_(store), commit_id_(commit_id) {}

auto SqlCertifier::certify(std::int64_t job_id) -> Result<Certificate> {
  try {
    auto transaction = cms::db::Transaction{client_};
    const auto certificate = certify_job(transaction.connection(), store_, job_id, commit_id_);
    if (!certificate) {
      transaction.rollback();
      return std::unexpected(certificate.error());
    }
    transaction.commit();
    return *certificate;
  } catch (const std::exception& failure) {
    return std::unexpected(Error::make(ErrorCode::unavailable, failure.what()));
  }
}

/// @brief Reads `locales.json` from the content root: every language's tag and the name a prompt says.
///
/// ADR 0031 §2 keeps a language's name out of the step, so it can only arrive as data; ADR 0056 puts that data beside
/// `topics.json`, where every other label already lives.
auto read_locale_declarations(const std::filesystem::path& content_root) -> Result<std::vector<LocaleDeclaration>> {
  const auto file = content_root / "locales.json";
  auto stream = std::ifstream{file, std::ios::binary};
  if (!stream) {
    return std::unexpected(Error::make(ErrorCode::unavailable,
                                       "the locale declarations cannot be read: " + file.string()));
  }
  auto buffer = std::ostringstream{};
  buffer << stream.rdbuf();
  const auto text = buffer.str();
  const auto padded = simdjson::padded_string{text};
  auto parser = simdjson::dom::parser{};
  auto document = simdjson::dom::element{};
  if (parser.parse(padded).get(document) != simdjson::SUCCESS) {
    return std::unexpected(refused("the locale declarations are not valid JSON: " + file.string()));
  }
  auto entries = simdjson::dom::array{};
  if (document.at_key("locales").get_array().get(entries) != simdjson::SUCCESS) {
    return std::unexpected(refused("the locale declarations carry no locales array: " + file.string()));
  }
  auto declared = std::vector<LocaleDeclaration>{};
  for (const auto entry : entries) {
    auto object = simdjson::dom::object{};
    if (entry.get_object().get(object) != simdjson::SUCCESS) {
      continue;
    }
    const auto tag = detail::field_text(object, "tag");
    const auto name = detail::field_text(object, "name");
    if (!tag || tag->empty() || !name || name->empty()) {
      return std::unexpected(refused("a locale in " + file.string() + " carries no tag or no display name"));
    }
    declared.push_back(LocaleDeclaration{.tag = *tag, .name = *name});
  }
  if (declared.empty()) {
    return std::unexpected(refused("the locale declarations name no language: " + file.string()));
  }
  return declared;
}

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
                  PipelinePorts ports, const RunnerFacts& facts) -> Result<PipelineReport> {
  const auto claimed = claim_job(database, worker);
  if (!claimed) {
    return std::unexpected(claimed.error());
  }
  if (!claimed->has_value()) {
    return PipelineReport{};
  }
  const auto claim = **claimed;
  auto report = PipelineReport{.job_id = claim.id, .state = {}, .steps = {}};

  // A refusal ends the attempt, never leaves the job running: the step's words go into the record as one inline
  // artefact row and the claim ends failed, so a reader of the job sees why without reading a log.
  const auto refuse = [&](std::int16_t step, std::string_view step_name,
                          const Error& error) -> Error {
    const auto artefact = Artefact{.step = step,
                                   .kind = std::string{REFUSAL_KIND},
                                   .locale = {},
                                   .home = ArtefactHome::row,
                                   .object_key = {},
                                   .bytes = error.message,
                                   .call = ModelCall{}};
    const auto recorded = ports.artefacts.record(claim.id, {artefact});
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
    return std::unexpected(refuse(JUDGE_STEP, "judge", judged.error()));
  }
  const auto recorded_candidates = ports.candidates.record(claim.id, *judged);
  if (!recorded_candidates) {
    return std::unexpected(refuse(JUDGE_STEP, "judge", recorded_candidates.error()));
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
  const auto recorded_specification = ports.specifications.record(claim.id, *answer, *checked);
  if (!recorded_specification) {
    return std::unexpected(refuse(SPECIFICATION_STEP, "specification", recorded_specification.error()));
  }
  report.steps.emplace_back("specification");

  const auto second = run_second_half(client, database, worker, claim, ports, facts);
  if (!second) {
    return std::unexpected(second.error());
  }
  report.state = second->state;
  report.steps.insert(report.steps.end(), second->steps.begin(), second->steps.end());
  return report;
}

auto run_second_half(ModelClient& client, cms::db::connection& database, std::string_view worker,
                     const JobClaim& claim, PipelinePorts ports, const RunnerFacts& facts)
    -> Result<PipelineReport> {
  auto report = PipelineReport{.job_id = claim.id, .state = {}, .steps = {}};

  const auto refuse = [&](std::int16_t step, std::string_view step_name,
                          const Error& error) -> Error {
    const auto artefact = Artefact{.step = step,
                                   .kind = std::string{REFUSAL_KIND},
                                   .locale = {},
                                   .home = ArtefactHome::row,
                                   .object_key = {},
                                   .bytes = error.message,
                                   .call = ModelCall{}};
    const auto recorded = ports.artefacts.record(claim.id, {artefact});
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

  const auto specification = read_specification_record(ports.client, database, ports.store, claim.id);
  if (!specification) {
    return std::unexpected(refuse(STATEMENT_STEP, "the specification", specification.error()));
  }
  const auto oracle = read_step_artefact(ports.client, ports.store, claim.id, SPECIFICATION_STEP, ORACLE_KIND);
  if (!oracle) {
    return std::unexpected(refuse(STATEMENT_STEP, "the oracle", oracle.error()));
  }
  const auto brief = load_brief(database, claim);
  if (!brief) {
    return std::unexpected(refuse(STATEMENT_STEP, "the brief", brief.error()));
  }
  const auto chosen = read_chosen_texts(database, claim.id);
  if (!chosen) {
    return std::unexpected(refuse(STATEMENT_STEP, "the plan", chosen.error()));
  }
  const auto technique = read_technique_names(facts.content_root / "topics.json", brief->technique);
  if (!technique) {
    return std::unexpected(refuse(STATEMENT_STEP, "the technique", technique.error()));
  }
  const auto declared = read_locale_declarations(facts.content_root);
  if (!declared) {
    return std::unexpected(refuse(STATEMENT_STEP, "the locales", declared.error()));
  }
  auto locales = std::vector<LocaleDeclaration>{};
  for (const auto& locale : brief->locales) {
    const auto found = std::find_if(declared->begin(), declared->end(),
                                    [&](const LocaleDeclaration& entry) { return entry.tag == locale; });
    if (found == declared->end()) {
      return std::unexpected(refuse(STATEMENT_STEP, "the locales",
                                    refused("the task ships locale \"" + locale +
                                            "\" and content/locales.json does not declare it, so no prompt can name "
                                            "the language it asks for")));
    }
    locales.push_back(*found);
  }
  const auto smallest = smallest_case(specification->specification.bounds);
  if (!smallest) {
    return std::unexpected(refuse(STATEMENT_STEP, "the specification", smallest.error()));
  }
  const auto oracle_check = check_oracle(oracle->bytes, smallest->input, specification->time_limit_ms);
  if (!oracle_check) {
    return std::unexpected(refuse(STATEMENT_STEP, "the oracle", oracle_check.error()));
  }
  const auto trap = trap_sentence(chosen->plan_json);
  if (!trap) {
    return std::unexpected(refuse(STATEMENT_STEP, "the plan", trap.error()));
  }
  const auto request = read_statement_request(ports.client, claim.id);
  if (!request) {
    return std::unexpected(refuse(STATEMENT_STEP, "the request", request.error()));
  }
  const auto statement_input = StatementInput{.specification = specification->specification,
                                              .trap = *trap,
                                              .technique = *technique,
                                              .locales = locales};
  const auto written = write_statement(client, statement_input, *oracle_check);
  if (!written) {
    return std::unexpected(refuse(STATEMENT_STEP, "statement", written.error()));
  }
  const auto subjects = leak_subjects(*written);
  const auto brief_check = check_brief_leak(subjects, *request, chosen->texts);
  if (!brief_check) {
    return std::unexpected(refuse(STATEMENT_STEP, "the brief leak check", brief_check.error()));
  }
  const auto algo_check = check_algo_leak(subjects, *technique);
  if (!algo_check) {
    return std::unexpected(refuse(STATEMENT_STEP, "the algorithm leak check", algo_check.error()));
  }
  const auto proof = LeakProof{.is_brief_checked = true, .is_algo_checked = true};
  const auto recorded_statement = ports.statements.record(claim.id, *written, *oracle_check, proof);
  if (!recorded_statement) {
    return std::unexpected(refuse(STATEMENT_STEP, "statement", recorded_statement.error()));
  }
  report.steps.emplace_back("statement (" + std::to_string(written->statements.size()) + " locale(s))");

  for (const auto& locale : locales) {
    const auto read_back =
        read_artefact(ports.client, ports.store, claim.id, STATEMENT_STEP, STATEMENT_KIND, locale.tag);
    if (!read_back) {
      return std::unexpected(refuse(SOLUTION_STEP, "the statement", read_back.error()));
    }
  }

  const auto solutions = write_solutions(client, specification->specification, oracle->bytes);
  if (!solutions) {
    return std::unexpected(refuse(SOLUTION_STEP, "solution", solutions.error()));
  }
  auto cases = std::vector<SmallCase>{};
  for (const auto& subtask : specification->specification.subtasks) {
    const auto admitted = admissible_cases(specification->specification, subtask);
    if (!admitted) {
      return std::unexpected(refuse(SOLUTION_STEP, "solution", admitted.error()));
    }
    cases.insert(cases.end(), admitted->begin(), admitted->end());
  }
  const auto proofs = check_solutions(*solutions, specification->specification, oracle->bytes, cases,
                                      specification->time_limit_ms);
  if (!proofs) {
    return std::unexpected(refuse(SOLUTION_STEP, "solution", proofs.error()));
  }
  const auto recorded_solutions = ports.solutions.record(claim.id, *solutions, *proofs);
  if (!recorded_solutions) {
    return std::unexpected(refuse(SOLUTION_STEP, "solution", recorded_solutions.error()));
  }
  report.steps.emplace_back("solution (" + std::to_string(solutions->partials.size()) + " subtask(s))");

  const auto reference = read_artefact(ports.client, ports.store, claim.id, SOLUTION_STEP, REFERENCE_KIND);
  if (!reference) {
    return std::unexpected(refuse(TEST_PLAN_STEP, "the reference", reference.error()));
  }

  auto partials = std::vector<std::pair<std::int32_t, std::string>>{};
  for (const auto& partial : solutions->partials) {
    partials.emplace_back(partial.position, partial.source);
  }
  const auto plan = write_test_plan(client, specification->specification, oracle->bytes,
                                    solutions->reference.source, partials, specification->time_limit_ms);
  if (!plan) {
    return std::unexpected(refuse(TEST_PLAN_STEP, "test plan", plan.error()));
  }
  const auto recorded_plan = ports.test_plans.record(claim.id, plan->answer, plan->scores);
  if (!recorded_plan) {
    return std::unexpected(refuse(TEST_PLAN_STEP, "test plan", recorded_plan.error()));
  }
  report.steps.emplace_back("test plan (" + std::to_string(plan->answer.cases.size()) + " case(s))");

  // The certifier reads a job whose evidence is complete and no worker is still on it, so the attempt ends in
  // awaiting-evidence first; the certificate then decides whether that evidence was worth a task.
  const auto awaiting = finish_claim(database, claim.id, worker, AWAITING_EVIDENCE);
  if (!awaiting) {
    return std::unexpected(awaiting.error());
  }
  const auto certificate = ports.certifier.certify(claim.id);
  if (!certificate) {
    const auto ended = mark_job(database, claim.id, FAILED);
    auto message = "step " + std::to_string(CERTIFICATE_STEP) + " (the certificate): " + certificate.error().message;
    if (!ended) {
      message += "; the job could not be moved to failed: " + ended.error().message;
    }
    return std::unexpected(Error::make(certificate.error().code, message));
  }
  report.steps.emplace_back("certificate (" + std::string{outcome_name(certificate->outcome)} + ")");
  if (certificate->outcome == VerificationOutcome::verified) {
    const auto marked = mark_job(database, claim.id, DONE);
    if (!marked) {
      return std::unexpected(marked.error());
    }
    report.state = std::string{DONE};
    return report;
  }

  auto checks = std::string{};
  for (const auto& check : certificate->checks) {
    if (!checks.empty()) {
      checks += "; ";
    }
    checks += check;
  }
  const auto refusal = Artefact{.step = CERTIFICATE_STEP,
                                .kind = std::string{REFUSAL_KIND},
                                .locale = {},
                                .home = ArtefactHome::row,
                                .object_key = {},
                                .bytes = "the certifier wrote " + std::string{outcome_name(certificate->outcome)} +
                                         ", so the task may not be reached: " + checks,
                                .call = ModelCall{}};
  const auto recorded = ports.artefacts.record(claim.id, {refusal});
  const auto ended = mark_job(database, claim.id, FAILED);
  auto message = "step " + std::to_string(CERTIFICATE_STEP) + " (the certificate): the certifier wrote " +
                 std::string{outcome_name(certificate->outcome)} + ", so the task may not be reached: " + checks;
  if (!recorded) {
    message += "; the refusal could not be recorded: " + recorded.error().message;
  }
  if (!ended) {
    message += "; the job could not be moved to failed: " + ended.error().message;
  }
  return std::unexpected(Error::make(ErrorCode::invalid_argument, message));
}

}  // namespace cms::generate
