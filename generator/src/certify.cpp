#include <cms/generate/certify.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <sqlpp23/sqlpp23.h>

#include <cms/db/connection.hpp>
#include <cms/db/tables.hpp>
#include <cms/generate/specification.hpp>

#include "json.hpp"

namespace cms::generate {
namespace {

using detail::field_integer;
using detail::field_text;
using detail::with_object;

auto refused(std::string_view message) -> Error {
  return Error::make(ErrorCode::invalid_argument, message);
}

/// @return @p text escaped for the inside of a JSON string.
auto escaped(std::string_view text) -> std::string {
  auto out = std::string{};
  out.reserve(text.size());
  for (const char character : text) {
    if (character == '"' || character == '\\') {
      out.push_back('\\');
    }
    if (character == '\n') {
      out += "\\n";
      continue;
    }
    out.push_back(character);
  }
  return out;
}

auto as_json_string(std::string_view text) -> std::string { return "\"" + escaped(text) + "\""; }

/// One artefact an earlier step cited, with the address its bytes must hash to.
struct Cited {
  std::int64_t id{0};
  std::int16_t step{0};
  std::string kind;
  std::optional<std::string> object_key;
  std::optional<std::string> inline_text;
  std::string digest;
};

struct Evidence {
  std::string task_id;
  std::int32_t task_version{0};
  std::string state;
  std::string specification_json;
  std::int64_t spec_time_limit_ms{0};
  std::int64_t spec_memory_limit_mb{0};
  std::int64_t task_time_limit_ms{0};
  std::int64_t task_memory_limit_mb{0};
  std::string task_content_hash;
  std::int64_t oracle_artefact{0};
  std::vector<std::pair<std::int32_t, std::int64_t>> solutions;
  std::vector<std::pair<std::string, std::int64_t>> programs;
  std::vector<std::pair<std::int32_t, std::int64_t>> cases;
  std::vector<std::int32_t> subtasks;
};

auto load_job(cms::db::connection& database, std::int64_t job_id, Evidence& evidence) -> Result<void> {
  auto found = false;
  for (const auto& row : database(sqlpp::select(cms::db::generationJobs.taskId,
                                                cms::db::generationJobs.taskVersion,
                                                cms::db::generationJobs.state)
                                      .from(cms::db::generationJobs)
                                      .where(cms::db::generationJobs.id == job_id))) {
    evidence.task_id = std::string{row.taskId};
    evidence.task_version = static_cast<std::int32_t>(row.taskVersion);
    evidence.state = std::string{row.state};
    found = true;
  }
  if (!found) {
    return std::unexpected(Error::make(ErrorCode::not_found,
                                       "generation job " + std::to_string(job_id) + " does not exist"));
  }
  if (evidence.state != "awaiting-evidence") {
    return std::unexpected(refused("generation job " + std::to_string(job_id) + " is " + evidence.state +
                                   "; the certifier reads a job whose earlier steps are recorded"));
  }
  return {};
}

auto load_specification(cms::db::connection& database, std::int64_t job_id, Evidence& evidence)
    -> Result<void> {
  auto found = false;
  for (const auto& row : database(sqlpp::select(cms::db::generationSpecifications.specification,
                                                cms::db::generationSpecifications.timeLimitMs,
                                                cms::db::generationSpecifications.memoryLimitMb,
                                                cms::db::generationSpecifications.oracleArtefactId)
                                      .from(cms::db::generationSpecifications)
                                      .where(cms::db::generationSpecifications.jobId == job_id))) {
    evidence.specification_json = std::string{row.specification};
    evidence.spec_time_limit_ms = row.timeLimitMs;
    evidence.spec_memory_limit_mb = row.memoryLimitMb;
    evidence.oracle_artefact = row.oracleArtefactId;
    found = true;
  }
  if (!found) {
    return std::unexpected(refused("generation job " + std::to_string(job_id) +
                                   " has no specification: step 3 wrote no evidence to certify"));
  }
  return {};
}

auto load_task(cms::db::connection& database, Evidence& evidence) -> Result<void> {
  auto found = false;
  for (const auto& row : database(sqlpp::select(cms::db::tasks.timeLimitMs, cms::db::tasks.memoryLimitMb,
                                                cms::db::tasks.contentHash)
                                      .from(cms::db::tasks)
                                      .where(cms::db::tasks.id == evidence.task_id &&
                                             cms::db::tasks.version ==
                                                 static_cast<std::int64_t>(evidence.task_version)))) {
    evidence.task_time_limit_ms = row.timeLimitMs;
    evidence.task_memory_limit_mb = row.memoryLimitMb;
    evidence.task_content_hash = std::string{row.contentHash};
    found = true;
  }
  if (!found) {
    return std::unexpected(Error::make(ErrorCode::not_found,
                                       "task " + evidence.task_id + " version " +
                                           std::to_string(evidence.task_version) + " does not exist"));
  }
  return {};
}

auto load_children(cms::db::connection& database, std::int64_t job_id, Evidence& evidence) -> void {
  for (const auto& row : database(sqlpp::select(cms::db::taskSubtasks.position)
                                      .from(cms::db::taskSubtasks)
                                      .where(cms::db::taskSubtasks.taskId == evidence.task_id &&
                                             cms::db::taskSubtasks.taskVersion ==
                                                 static_cast<std::int64_t>(evidence.task_version))
                                      .order_by(cms::db::taskSubtasks.position.asc()))) {
    evidence.subtasks.push_back(static_cast<std::int32_t>(row.position));
  }
  for (const auto& row : database(sqlpp::select(cms::db::generationSolutions.position,
                                                cms::db::generationSolutions.artefactId)
                                      .from(cms::db::generationSolutions)
                                      .where(cms::db::generationSolutions.jobId == job_id)
                                      .order_by(cms::db::generationSolutions.position.asc()))) {
    evidence.solutions.emplace_back(static_cast<std::int32_t>(row.position), row.artefactId);
  }
  for (const auto& row : database(sqlpp::select(cms::db::generationPrograms.role,
                                                cms::db::generationPrograms.artefactId)
                                      .from(cms::db::generationPrograms)
                                      .where(cms::db::generationPrograms.jobId == job_id))) {
    evidence.programs.emplace_back(std::string{row.role}, row.artefactId);
  }
  for (const auto& row : database(sqlpp::select(cms::db::generationCases.subtaskPosition,
                                                cms::db::generationCases.artefactId)
                                      .from(cms::db::generationCases)
                                      .where(cms::db::generationCases.jobId == job_id)
                                      .order_by(cms::db::generationCases.subtaskPosition.asc()))) {
    evidence.cases.emplace_back(static_cast<std::int32_t>(row.subtaskPosition), row.artefactId);
  }
}

auto cited_artefact(cms::db::connection& database, std::int64_t id) -> Result<Cited> {
  for (const auto& row : database(sqlpp::select(cms::db::generationArtefacts.step,
                                                cms::db::generationArtefacts.kind,
                                                cms::db::generationArtefacts.objectKey,
                                                cms::db::generationArtefacts.inlineText,
                                                cms::db::generationArtefacts.digest)
                                      .from(cms::db::generationArtefacts)
                                      .where(cms::db::generationArtefacts.id == id))) {
    return Cited{.id = id,
                 .step = static_cast<std::int16_t>(row.step),
                 .kind = std::string{row.kind},
                 .object_key = row.objectKey.has_value() ? std::optional<std::string>{std::string{*row.objectKey}}
                                                         : std::nullopt,
                 .inline_text = row.inlineText.has_value()
                                    ? std::optional<std::string>{std::string{*row.inlineText}}
                                    : std::nullopt,
                 .digest = std::string{row.digest}};
  }
  return std::unexpected(Error::make(ErrorCode::not_found,
                                     "artefact " + std::to_string(id) + " does not exist"));
}

/// Reads an artefact back the way a reader would: through the digest its row records, never a plain get.
auto verified_bytes(const cms::store::ObjectStore& store, const Cited& cited, std::string& bytes)
    -> Result<void> {
  if (cited.object_key.has_value()) {
    const auto read = store.read_verified(*cited.object_key, cited.digest);
    if (!read) {
      return std::unexpected(Error::make(
          read.error().code, "step " + std::to_string(cited.step) + " " + cited.kind + " at " +
                                 *cited.object_key + ": " + read.error().message));
    }
    bytes = *read;
    return {};
  }
  if (cited.inline_text.has_value()) {
    const auto stored = cms::store::digest_of(*cited.inline_text);
    if (stored != cited.digest) {
      return std::unexpected(Error::make(
          ErrorCode::conflict, "step " + std::to_string(cited.step) + " " + cited.kind +
                                   " no longer hashes to its row: the row says " + cited.digest +
                                   ", the text hashes to " + stored));
    }
    bytes = *cited.inline_text;
    return {};
  }
  return std::unexpected(refused("step " + std::to_string(cited.step) + " " + cited.kind +
                                 " names neither an object nor inline text"));
}

auto parse_specification(std::string_view json) -> Result<Specification> {
  auto specification = Specification{};
  auto problem = std::string{};
  const auto parsed = with_object(json, "the specification", [&](const simdjson::dom::object& object) -> Result<void> {
    const auto input = field_text(object, "input");
    const auto output = field_text(object, "output");
    auto bounds = simdjson::dom::object{};
    if (!input || !output) {
      problem = "the specification names no input or output";
      return {};
    }
    if (object.at_key("bounds").get_object().get(bounds) != simdjson::SUCCESS) {
      problem = "the specification carries no bounds";
      return {};
    }
    const auto min_n = field_integer(bounds, "min_n");
    const auto max_n = field_integer(bounds, "max_n");
    const auto value_min = field_integer(bounds, "value_min");
    const auto value_max = field_integer(bounds, "value_max");
    if (!min_n || !max_n || !value_min || !value_max) {
      problem = "the specification's bounds are incomplete";
      return {};
    }
    specification.input = *input;
    specification.output = *output;
    specification.bounds = Bounds{.min_n = *min_n,
                                  .max_n = *max_n,
                                  .value_min = *value_min,
                                  .value_max = *value_max};
    auto subtasks = simdjson::dom::array{};
    if (object.at_key("subtasks").get_array().get(subtasks) == simdjson::SUCCESS) {
      for (const auto element : subtasks) {
        auto entry = simdjson::dom::object{};
        if (element.get_object().get(entry) != simdjson::SUCCESS) {
          problem = "a stored subtask is not an object";
          return {};
        }
        const auto position = field_integer(entry, "position");
        const auto constraint = field_text(entry, "constraint");
        if (!position || !constraint) {
          problem = "a stored subtask names no position or constraint";
          return {};
        }
        specification.subtasks.push_back(
            SubtaskConstraint{.position = static_cast<std::int32_t>(*position), .constraint = *constraint});
      }
    }
    return {};
  });
  if (!parsed) {
    return std::unexpected(parsed.error());
  }
  if (!problem.empty()) {
    return std::unexpected(refused(problem));
  }
  return specification;
}

}  // namespace

auto outcome_name(VerificationOutcome outcome) -> std::string_view {
  switch (outcome) {
    case VerificationOutcome::verified:
      return "verified";
    case VerificationOutcome::failed:
      return "failed";
    case VerificationOutcome::pending:
      return "pending";
  }
  return "pending";
}

auto certify_job(cms::db::connection& database, const cms::store::ObjectStore& store, std::int64_t job_id,
                 std::string_view commit_id) -> Result<Certificate> {
  if (commit_id.empty()) {
    return std::unexpected(refused("a certificate names the commit it checked; this one names none"));
  }

  auto evidence = Evidence{};
  if (const auto step = load_job(database, job_id, evidence); !step) {
    return std::unexpected(step.error());
  }
  if (const auto step = load_specification(database, job_id, evidence); !step) {
    return std::unexpected(step.error());
  }
  if (const auto step = load_task(database, evidence); !step) {
    return std::unexpected(step.error());
  }
  load_children(database, job_id, evidence);

  auto certificate = Certificate{.job_id = job_id,
                                 .task_id = evidence.task_id,
                                 .task_version = evidence.task_version,
                                 .outcome = VerificationOutcome::pending,
                                 .commit_id = std::string{commit_id},
                                 .checks = {},
                                 .digests = {}};

  // Step 3 staged the limits the task now carries; a task row that moved since is evidence from another run.
  if (evidence.spec_time_limit_ms != evidence.task_time_limit_ms ||
      evidence.spec_memory_limit_mb != evidence.task_memory_limit_mb) {
    return std::unexpected(refused(
        "task " + evidence.task_id + " version " + std::to_string(evidence.task_version) + " runs at " +
        std::to_string(evidence.task_time_limit_ms) + " ms and " + std::to_string(evidence.task_memory_limit_mb) +
        " MB, and step 3 derived " + std::to_string(evidence.spec_time_limit_ms) + " ms and " +
        std::to_string(evidence.spec_memory_limit_mb) + " MB; the evidence is stale"));
  }

  // Step 5 wrote the reference (position 0) and one solution per subtask; the task's subtasks must still be that set.
  for (const auto position : evidence.subtasks) {
    const auto found = std::any_of(evidence.solutions.begin(), evidence.solutions.end(),
                                   [position](const auto& entry) { return entry.first == position; });
    if (!found) {
      return std::unexpected(refused("task " + evidence.task_id + " version " +
                                     std::to_string(evidence.task_version) + " has subtask " +
                                     std::to_string(position) + ", and step 5 wrote no solution for it"));
    }
  }
  const auto has_reference = std::any_of(evidence.solutions.begin(), evidence.solutions.end(),
                                         [](const auto& entry) { return entry.first == 0; });
  if (!has_reference) {
    return std::unexpected(refused("generation job " + std::to_string(job_id) +
                                   " has no reference solution (step 5)"));
  }
  for (const auto& [position, artefact] : evidence.solutions) {
    (void)artefact;
    const auto known = position == 0 ||
                       std::any_of(evidence.subtasks.begin(), evidence.subtasks.end(),
                                   [position](std::int32_t subtask) { return subtask == position; });
    if (!known) {
      return std::unexpected(refused("step 5 wrote a solution for position " + std::to_string(position) +
                                     ", which the task does not have"));
    }
  }

  // Step 6 wrote both programs and one case set per subtask.
  for (const auto role : {std::string_view{"generator"}, std::string_view{"checker"}}) {
    const auto found = std::any_of(evidence.programs.begin(), evidence.programs.end(),
                                   [role](const auto& entry) { return entry.first == role; });
    if (!found) {
      return std::unexpected(refused("generation job " + std::to_string(job_id) + " has no " +
                                     std::string{role} + " (step 6)"));
    }
  }
  for (const auto position : evidence.subtasks) {
    const auto found = std::any_of(evidence.cases.begin(), evidence.cases.end(),
                                   [position](const auto& entry) { return entry.first == position; });
    if (!found) {
      return std::unexpected(refused("generation job " + std::to_string(job_id) + " has no cases for subtask " +
                                     std::to_string(position) + " (step 6)"));
    }
  }

  // Every cited artefact is read back at the address its row records.
  auto ids = std::vector<std::int64_t>{evidence.oracle_artefact};
  for (const auto& entry : evidence.solutions) {
    ids.push_back(entry.second);
  }
  for (const auto& entry : evidence.programs) {
    ids.push_back(entry.second);
  }
  for (const auto& entry : evidence.cases) {
    ids.push_back(entry.second);
  }
  auto sources = std::vector<std::pair<std::int64_t, std::string>>{};
  for (const auto id : ids) {
    const auto cited = cited_artefact(database, id);
    if (!cited) {
      return std::unexpected(cited.error());
    }
    auto bytes = std::string{};
    if (const auto read = verified_bytes(store, *cited, bytes); !read) {
      return std::unexpected(read.error());
    }
    certificate.digests.push_back(cited->digest);
    sources.emplace_back(id, std::move(bytes));
  }
  certificate.checks.push_back("step 3, 5 and 6 evidence read back at the digest its row records (" +
                               std::to_string(certificate.digests.size()) + " artefact(s))");

  // The one check that proves something by running it: the oracle and the reference agree on the smallest case.
  const auto specification = parse_specification(evidence.specification_json);
  if (!specification) {
    return std::unexpected(specification.error());
  }
  const auto smallest = sample_smallest(*specification);
  if (!smallest) {
    return std::unexpected(smallest.error());
  }
  auto bytes_of = [&sources](std::int64_t id) {
    for (const auto& entry : sources) {
      if (entry.first == id) {
        return entry.second;
      }
    }
    return std::string{};
  };
  auto reference_artefact = std::int64_t{0};
  for (const auto& [position, artefact] : evidence.solutions) {
    if (position == 0) {
      reference_artefact = artefact;
    }
  }
  const auto oracle_answer =
      run_program(bytes_of(evidence.oracle_artefact), *smallest, evidence.spec_time_limit_ms);
  const auto reference_answer =
      run_program(bytes_of(reference_artefact), *smallest, evidence.spec_time_limit_ms);
  if (!oracle_answer || !reference_answer) {
    certificate.outcome = VerificationOutcome::failed;
    certificate.checks.push_back(
        "the oracle and the reference run on the smallest case: failed (" +
        std::string{(!oracle_answer ? oracle_answer.error().message : reference_answer.error().message)} + ")");
  } else if (*oracle_answer != *reference_answer) {
    certificate.outcome = VerificationOutcome::failed;
    certificate.checks.push_back("the oracle and the reference agree on the smallest case: no (the oracle says " +
                                 as_json_string(*oracle_answer) + ", the reference says " + as_json_string(*reference_answer) + ")");
  } else {
    certificate.outcome = VerificationOutcome::verified;
    certificate.checks.push_back("the oracle and the reference agree on the smallest case (" +
                                 as_json_string(*oracle_answer) + ")");
  }

  // The certificate and the verification move together: the caller commits both or neither.
  auto checks = std::string{"["};
  auto first_check = true;
  for (const auto& check : certificate.checks) {
    if (!first_check) {
      checks += ',';
    }
    first_check = false;
    checks += as_json_string(check);
  }
  checks += ']';
  auto evidence_json = std::string{"{\"task_content_hash\":" + as_json_string(evidence.task_content_hash) +
                                   ",\"artefact_digests\":["};
  auto first_digest = true;
  for (const auto& digest : certificate.digests) {
    if (!first_digest) {
      evidence_json += ',';
    }
    first_digest = false;
    evidence_json += as_json_string(digest);
  }
  evidence_json += "]}";
  database.execute_text(
      "INSERT INTO generation_certificates (job_id, task_id, task_version, outcome, checks, commit_id, evidence) "
      "VALUES ($1, $2, $3, $4::task_verification_status, $5::jsonb, $6, $7::jsonb)",
      {job_id, evidence.task_id, static_cast<std::int64_t>(evidence.task_version),
       std::string{outcome_name(certificate.outcome)}, checks, certificate.commit_id, evidence_json});
  database.execute_text(
      "UPDATE tasks SET verification = $3::task_verification_status WHERE id = $1 AND version = $2",
      {evidence.task_id, static_cast<std::int64_t>(evidence.task_version),
       std::string{outcome_name(certificate.outcome)}});
  return certificate;
}

}  // namespace cms::generate
