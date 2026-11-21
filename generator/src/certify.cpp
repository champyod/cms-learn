#include <cms/generate/certify.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <simdjson.h>
#include <sqlpp23/sqlpp23.h>

#include <cms/db/connection.hpp>
#include <cms/db/tables.hpp>
#include <cms/generate/specification.hpp>

namespace cms::generate {
namespace {

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

/// @return @p parts joined by @p separator.
auto joined(const std::vector<std::string>& parts, std::string_view separator) -> std::string {
  auto text = std::string{};
  for (const auto& part : parts) {
    text += text.empty() ? "" : std::string{separator};
    text += part;
  }
  return text;
}

/// The evidence a certificate records: the task content it certified and every artefact it read, with the digest each
/// hashed to. This is the only writer and the only reader of that shape, so the two cannot drift apart.
auto evidence_json(std::string_view content_hash, const std::vector<CertifiedArtefact>& artefacts) -> std::string {
  auto json = std::string{"{\"task_content_hash\":" + as_json_string(content_hash) + ",\"artefacts\":["};
  auto first = true;
  for (const auto& artefact : artefacts) {
    json += first ? "" : ",";
    first = false;
    json += "{\"role\":" + as_json_string(artefact.role) + ",\"artefact\":" +
            std::to_string(artefact.artefact) + ",\"digest\":" + as_json_string(artefact.digest) + "}";
  }
  json += "]}";
  return json;
}

/// @return The evidence @p json records, or a refusal naming the part of the shape it cannot read.
auto read_evidence(std::string_view json) -> Result<std::pair<std::string, std::vector<CertifiedArtefact>>> {
  auto padded = simdjson::padded_string{json};
  auto parser = simdjson::dom::parser{};
  auto document = simdjson::dom::element{};
  if (parser.parse(padded).get(document) != simdjson::SUCCESS) {
    return std::unexpected(refused("the certificate's evidence is not valid JSON"));
  }
  auto object = simdjson::dom::object{};
  if (document.get_object().get(object) != simdjson::SUCCESS) {
    return std::unexpected(refused("the certificate's evidence is not a JSON object"));
  }
  auto content_hash = std::string_view{};
  if (object.at_key("task_content_hash").get_string().get(content_hash) != simdjson::SUCCESS) {
    return std::unexpected(refused("the certificate's evidence names no task content hash"));
  }
  auto entries = simdjson::dom::array{};
  if (object.at_key("artefacts").get_array().get(entries) != simdjson::SUCCESS) {
    return std::unexpected(refused("the certificate's evidence names no artefacts, so nothing it certified can be read"));
  }
  auto artefacts = std::vector<CertifiedArtefact>{};
  for (const auto entry : entries) {
    auto recorded = simdjson::dom::object{};
    if (entry.get_object().get(recorded) != simdjson::SUCCESS) {
      return std::unexpected(refused("a recorded artefact is not a JSON object"));
    }
    auto role = std::string_view{};
    auto digest = std::string_view{};
    auto id = std::int64_t{0};
    if (recorded.at_key("role").get_string().get(role) != simdjson::SUCCESS ||
        recorded.at_key("digest").get_string().get(digest) != simdjson::SUCCESS ||
        recorded.at_key("artefact").get_int64().get(id) != simdjson::SUCCESS) {
      return std::unexpected(refused("a recorded artefact names no role, id or digest"));
    }
    artefacts.push_back(CertifiedArtefact{.role = std::string{role}, .artefact = id, .digest = std::string{digest}});
  }
  return std::pair{std::string{content_hash}, std::move(artefacts)};
}

/// One artefact an earlier step cited, with the address its bytes must hash to.
struct Cited {
  std::int64_t id{0};
  std::int16_t step{0};
  std::string kind;
  std::optional<std::string> object_key;
  std::optional<std::string> inline_text;
  std::string digest;
};

/// One case set step 6 recorded for a subtask, and how many cases it said the set holds.
struct CaseSet {
  std::int32_t position{0};
  std::int64_t artefact{0};
  std::int64_t count{0};
};

struct Evidence {
  std::string task_id;
  std::int32_t task_version{0};
  std::string state;
  std::string specification_json;
  std::int64_t spec_time_limit_ms{0};
  std::int64_t spec_memory_used_limit_mb{0};
  std::int64_t spec_memory_reserved_limit_mb{0};
  std::int64_t task_time_limit_ms{0};
  std::int64_t task_memory_used_limit_mb{0};
  std::int64_t task_memory_reserved_limit_mb{0};
  std::string task_content_hash;
  std::int64_t oracle_artefact{0};
  std::vector<std::pair<std::int32_t, std::int64_t>> solutions;
  std::vector<std::pair<std::string, std::int64_t>> programs;
  std::vector<CaseSet> cases;
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
                                                cms::db::generationSpecifications.memoryUsedLimitMb,
                                                cms::db::generationSpecifications.memoryReservedLimitMb,
                                                cms::db::generationSpecifications.oracleArtefactId)
                                      .from(cms::db::generationSpecifications)
                                      .where(cms::db::generationSpecifications.jobId == job_id))) {
    evidence.specification_json = std::string{row.specification};
    evidence.spec_time_limit_ms = row.timeLimitMs;
    evidence.spec_memory_used_limit_mb = row.memoryUsedLimitMb;
    evidence.spec_memory_reserved_limit_mb = row.memoryReservedLimitMb;
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
  for (const auto& row : database(sqlpp::select(cms::db::tasks.timeLimitMs, cms::db::tasks.memoryUsedLimitMb,
                                                cms::db::tasks.memoryReservedLimitMb, cms::db::tasks.contentHash)
                                      .from(cms::db::tasks)
                                      .where(cms::db::tasks.id == evidence.task_id &&
                                             cms::db::tasks.version ==
                                                 static_cast<std::int64_t>(evidence.task_version)))) {
    evidence.task_time_limit_ms = row.timeLimitMs;
    evidence.task_memory_used_limit_mb = row.memoryUsedLimitMb;
    evidence.task_memory_reserved_limit_mb = row.memoryReservedLimitMb;
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
                                                cms::db::generationCases.artefactId,
                                                cms::db::generationCases.caseCount)
                                      .from(cms::db::generationCases)
                                      .where(cms::db::generationCases.jobId == job_id)
                                      .order_by(cms::db::generationCases.subtaskPosition.asc()))) {
    evidence.cases.push_back(CaseSet{.position = static_cast<std::int32_t>(row.subtaskPosition),
                                     .artefact = row.artefactId,
                                     .count = row.caseCount});
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

/// @return The outcome @p name records; `pending` for a name the enum does not hold.
auto outcome_named(std::string_view name) -> VerificationOutcome {
  if (name == "verified") {
    return VerificationOutcome::verified;
  }
  if (name == "failed") {
    return VerificationOutcome::failed;
  }
  return VerificationOutcome::pending;
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

auto revalidate_certificate(cms::db::connection& database, const cms::store::ObjectStore& store,
                            std::string_view task_id, std::int32_t task_version) -> Result<CertificateState> {
  auto state = CertificateState{};
  auto job_id = std::int64_t{0};
  auto recorded_outcome = std::string{};
  auto recorded_commit = std::string{};
  auto recorded_evidence = std::string{};
  for (const auto& row : database(sqlpp::select(cms::db::generationCertificates.jobId,
                                                cms::db::generationCertificates.outcome,
                                                cms::db::generationCertificates.commitId,
                                                cms::db::generationCertificates.evidence)
                                      .from(cms::db::generationCertificates)
                                      .where(cms::db::generationCertificates.taskId == std::string{task_id} &&
                                             cms::db::generationCertificates.taskVersion ==
                                                 static_cast<std::int64_t>(task_version))
                                      .order_by(cms::db::generationCertificates.createdAt.desc())
                                      .limit(1))) {
    // A hand-written task's certificate names no job (ADR 0057): its evidence is the task's own files, not a job's.
    job_id = row.jobId.value_or(0);
    recorded_outcome = std::string{row.outcome};
    recorded_commit = std::string{row.commitId};
    recorded_evidence = std::string{row.evidence};
  }
  if (recorded_commit.empty() && recorded_outcome.empty()) {
    return state;
  }
  const auto recorded = read_evidence(recorded_evidence);
  if (!recorded) {
    return std::unexpected(recorded.error());
  }
  auto current_hash = std::string{};
  for (const auto& row : database(sqlpp::select(cms::db::tasks.contentHash)
                                      .from(cms::db::tasks)
                                      .where(cms::db::tasks.id == std::string{task_id} &&
                                             cms::db::tasks.version == static_cast<std::int64_t>(task_version)))) {
    current_hash = std::string{row.contentHash};
  }
  auto changed = std::vector<std::string>{};
  if (current_hash != recorded->first) {
    changed.push_back("the task's content recorded " + recorded->first + ", and the row now holds " + current_hash);
  }
  auto current = std::vector<CertifiedArtefact>{};
  for (const auto& artefact : recorded->second) {
    const auto cited = cited_artefact(database, artefact.artefact);
    if (!cited) {
      changed.push_back(artefact.role + ": " + cited.error().message);
      continue;
    }
    if (cited->digest != artefact.digest) {
      changed.push_back(artefact.role + " recorded digest " + artefact.digest + ", and its row now holds " +
                        cited->digest);
      current.push_back(CertifiedArtefact{.role = artefact.role, .artefact = artefact.artefact, .digest = cited->digest});
      continue;
    }
    auto bytes = std::string{};
    if (const auto read = verified_bytes(store, *cited, bytes); !read) {
      changed.push_back(artefact.role + ": " + read.error().message);
      continue;
    }
    current.push_back(CertifiedArtefact{.role = artefact.role, .artefact = artefact.artefact, .digest = cited->digest});
  }
  state.is_certified = true;
  state.commit_id = recorded_commit;
  if (changed.empty()) {
    state.outcome = outcome_named(recorded_outcome);
    return state;
  }

  // The recorded outcome is ignored: a certificate whose artefacts moved certifies nothing. The failed row records
  // what changed and what the rows hold now, and the verification moves off `verified`, so the stale row cannot leave
  // the task certified (ADR 0046 §4).
  database.execute_text(
      "INSERT INTO generation_certificates (job_id, task_id, task_version, outcome, checks, commit_id, evidence) "
      "VALUES ($1, $2, $3, 'failed'::task_verification_status, $4::jsonb, $5, $6::jsonb)",
      {job_id, std::string{task_id}, static_cast<std::int64_t>(task_version),
       std::string{"[\"the certificate written by this task version no longer certifies its evidence: " +
                   joined(changed, "; ") + "\"]"},
       recorded_commit, evidence_json(current_hash, current)});
  database.execute_text(
      "UPDATE tasks SET verification = 'failed'::task_verification_status WHERE id = $1 AND version = $2",
      {std::string{task_id}, static_cast<std::int64_t>(task_version)});
  state.outcome = VerificationOutcome::failed;
  state.changed = std::move(changed);
  return state;
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

  // ADR 0046 §4: the digests the previous certificate recorded are read here, at the point this run decides an
  // outcome. A certificate whose evidence moved is recorded as failed and the task moves off verified before this
  // run decides; the new certificate's checks say what changed.
  const auto previous = revalidate_certificate(database, store, evidence.task_id, evidence.task_version);
  if (!previous) {
    return std::unexpected(previous.error());
  }

  auto certificate = Certificate{.job_id = job_id,
                                 .task_id = evidence.task_id,
                                 .task_version = evidence.task_version,
                                 .outcome = VerificationOutcome::pending,
                                 .commit_id = std::string{commit_id},
                                 .checks = {},
                                 .artefacts = {}};

  // Step 3 staged the limits the task now carries; a task row that moved since is evidence from another run.
  if (evidence.spec_time_limit_ms != evidence.task_time_limit_ms ||
      evidence.spec_memory_used_limit_mb != evidence.task_memory_used_limit_mb ||
      evidence.spec_memory_reserved_limit_mb != evidence.task_memory_reserved_limit_mb) {
    return std::unexpected(refused(
        "task " + evidence.task_id + " version " + std::to_string(evidence.task_version) + " runs at " +
        std::to_string(evidence.task_time_limit_ms) + " ms, " +
        std::to_string(evidence.task_memory_used_limit_mb) + " MB used and " +
        std::to_string(evidence.task_memory_reserved_limit_mb) + " MB reserved, and step 3 derived " +
        std::to_string(evidence.spec_time_limit_ms) + " ms, " +
        std::to_string(evidence.spec_memory_used_limit_mb) + " MB used and " +
        std::to_string(evidence.spec_memory_reserved_limit_mb) + " MB reserved; the evidence is stale"));
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
                                   [position](const CaseSet& entry) { return entry.position == position; });
    if (!found) {
      return std::unexpected(refused("generation job " + std::to_string(job_id) + " has no cases for subtask " +
                                     std::to_string(position) + " (step 6)"));
    }
  }

  // Every cited artefact is read back at the address its row records.
  auto read_back = [&database, &store, &certificate](std::int64_t id, std::string role) -> Result<std::string> {
    const auto cited = cited_artefact(database, id);
    if (!cited) {
      return std::unexpected(cited.error());
    }
    auto bytes = std::string{};
    if (const auto read = verified_bytes(store, *cited, bytes); !read) {
      return std::unexpected(read.error());
    }
    certificate.artefacts.push_back(
        CertifiedArtefact{.role = std::move(role), .artefact = id, .digest = cited->digest});
    return bytes;
  };
  const auto oracle_source = read_back(evidence.oracle_artefact, "step 3's oracle");
  if (!oracle_source) {
    return std::unexpected(oracle_source.error());
  }
  auto reference_artefact = std::int64_t{0};
  for (const auto& [position, artefact] : evidence.solutions) {
    if (position == 0) {
      reference_artefact = artefact;
    }
  }
  const auto reference_source = read_back(reference_artefact, "the reference (step 5, position 0)");
  if (!reference_source) {
    return std::unexpected(reference_source.error());
  }
  auto partial_sources = std::vector<std::pair<std::int32_t, std::string>>{};
  for (const auto& [position, artefact] : evidence.solutions) {
    if (position == 0) {
      continue;
    }
    const auto source = read_back(artefact, "the solution for subtask " + std::to_string(position));
    if (!source) {
      return std::unexpected(source.error());
    }
    partial_sources.emplace_back(position, *source);
  }
  auto artefact_of = [](const std::vector<std::pair<std::string, std::int64_t>>& rows,
                        std::string_view role) {
    for (const auto& entry : rows) {
      if (entry.first == role) {
        return entry.second;
      }
    }
    return std::int64_t{0};
  };
  const auto generator_source = read_back(artefact_of(evidence.programs, "generator"), "step 6's generator");
  if (!generator_source) {
    return std::unexpected(generator_source.error());
  }
  const auto checker_source = read_back(artefact_of(evidence.programs, "checker"), "step 6's checker");
  if (!checker_source) {
    return std::unexpected(checker_source.error());
  }
  for (const auto& entry : evidence.cases) {
    const auto cited = read_back(entry.artefact, "the cases for subtask " + std::to_string(entry.position));
    if (!cited) {
      return std::unexpected(cited.error());
    }
  }
  certificate.checks.push_back("step 3, 5 and 6 evidence read back at the digest its row records (" +
                               std::to_string(certificate.artefacts.size()) + " artefact(s))");
  if (!previous->changed.empty()) {
    certificate.checks.push_back("the certificate written before this run no longer certified its evidence (" +
                                 joined(previous->changed, "; ") + "), so this run re-ran what the rows hold now");
  }

  // Step 3's row is read by the one reader its writer uses, so the stored shape cannot drift from this program.
  const auto specification = read_specification(evidence.specification_json);
  if (!specification) {
    return std::unexpected(specification.error());
  }

  auto re_run = ReRunEvidence{.specification = *specification,
                              .generator_source = *generator_source,
                              .checker_source = *checker_source,
                              .oracle_source = *oracle_source,
                              .reference_source = *reference_source,
                              .partial_sources = std::move(partial_sources),
                              .recorded_case_counts = {},
                              .time_limit_ms = evidence.spec_time_limit_ms,
                              .memory_used_limit_mb = evidence.spec_memory_used_limit_mb,
                              .memory_reserved_limit_mb = evidence.spec_memory_reserved_limit_mb};
  for (const auto& entry : evidence.cases) {
    re_run.recorded_case_counts.emplace_back(entry.position, entry.count);
  }

  certificate.outcome = VerificationOutcome::verified;
  for (const auto& result : re_run_methods(re_run)) {
    if (!result.is_passed) {
      certificate.outcome = VerificationOutcome::failed;
    }
    certificate.checks.push_back(result.sentence);
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
  database.execute_text(
      "INSERT INTO generation_certificates (job_id, task_id, task_version, outcome, checks, commit_id, evidence) "
      "VALUES ($1, $2, $3, $4::task_verification_status, $5::jsonb, $6, $7::jsonb)",
      {job_id, evidence.task_id, static_cast<std::int64_t>(evidence.task_version),
       std::string{outcome_name(certificate.outcome)}, checks, certificate.commit_id,
       evidence_json(evidence.task_content_hash, certificate.artefacts)});
  database.execute_text(
      "UPDATE tasks SET verification = $3::task_verification_status WHERE id = $1 AND version = $2",
      {evidence.task_id, static_cast<std::int64_t>(evidence.task_version),
       std::string{outcome_name(certificate.outcome)}});
  return certificate;
}

}  // namespace cms::generate

namespace cms::generate {
namespace {

/// One example a hand-written task ships: the input and the answer it must produce.
struct Example {
  std::filesystem::path input_path;
  std::string input;
  std::string expected;
};

/// @return The reference's path exactly as the task's own `task.json` declares it under
/// `verification.reference_solution`, which is the key the content schema carries.
///
/// A refusal that names a file the task does not ship sends its reader to a path that is not there, so a task that
/// declares no reference is refused by name here rather than guessed at.
auto reference_path_of(const std::filesystem::path& task_root) -> Result<std::string> {
  auto stream = std::ifstream{task_root / "task.json", std::ios::binary};
  if (!stream) {
    return std::unexpected(Error::make(ErrorCode::not_found,
                                       "task " + task_root.filename().string() + " ships no task.json, so the file its "
                                       "reference comes from cannot be named"));
  }
  auto buffer = std::ostringstream{};
  buffer << stream.rdbuf();
  const auto json = buffer.str();
  auto parser = simdjson::dom::parser{};
  auto document = simdjson::dom::element{};
  if (parser.parse(json).get(document) != simdjson::SUCCESS) {
    return std::unexpected(Error::make(ErrorCode::invalid_argument,
                                       "task " + task_root.filename().string() + " has a task.json that does not parse, "
                                       "so the file its reference comes from cannot be named"));
  }
  auto named = std::string_view{};
  if (document["verification"]["reference_solution"].get_string().get(named) != simdjson::SUCCESS) {
    return std::unexpected(Error::make(ErrorCode::invalid_argument,
                                       "task " + task_root.filename().string() +
                                           " declares no verification.reference_solution, so the file its reference "
                                           "comes from cannot be named"));
  }
  return std::string{named};
}

auto read_file(const std::filesystem::path& path) -> Result<std::string> {
  auto stream = std::ifstream{path, std::ios::binary};
  if (!stream) {
    return std::unexpected(Error::make(ErrorCode::not_found, "the example " + path.string() + " cannot be read"));
  }
  auto buffer = std::ostringstream{};
  buffer << stream.rdbuf();
  return buffer.str();
}

/// @return Every example of @p task_root, in the order the file names sort, or a refusal naming the directory that holds
/// none. An example without its answer is refused rather than skipped: a task whose examples are half missing is a task
/// whose reference was never fully run.
auto examples_of(const std::filesystem::path& task_root) -> Result<std::vector<Example>> {
  const auto directory = task_root / "examples";
  auto paths = std::vector<std::filesystem::path>{};
  auto error = std::error_code{};
  for (const auto& entry : std::filesystem::directory_iterator{directory, error}) {
    if (entry.is_regular_file() && entry.path().extension() == ".in") {
      paths.push_back(entry.path());
    }
  }
  if (error || paths.empty()) {
    return std::unexpected(Error::make(ErrorCode::not_found,
                                       "task " + task_root.filename().string() + " ships no examples under " +
                                           (directory.string()) + ", so its reference has nothing to pass"));
  }
  std::sort(paths.begin(), paths.end());
  auto examples = std::vector<Example>{};
  for (const auto& path : paths) {
    auto expected_path = path;
    expected_path.replace_extension(".out");
    const auto input = read_file(path);
    if (!input) {
      return std::unexpected(input.error());
    }
    const auto expected = read_file(expected_path);
    if (!expected) {
      return std::unexpected(expected.error());
    }
    examples.push_back(Example{.input_path = path, .input = *input, .expected = *expected});
  }
  return examples;
}

/// @return The answer as it is compared: trailing spaces and newlines dropped, so a missing final newline is not a
/// mismatch that an author has to guess at.
auto trimmed(std::string_view text) -> std::string_view {
  const auto end = text.find_last_not_of(" \t\r\n");
  return end == std::string_view::npos ? std::string_view{} : text.substr(0, end + 1);
}

}  // namespace

auto certify_task(cms::db::connection& database, const std::filesystem::path& content_root, std::string_view task_id,
                  std::int32_t version, std::string_view commit_id) -> Result<Certificate> {
  auto task = std::optional<Certificate>{};
  auto time_limit_ms = std::int64_t{0};
  auto used_limit_mb = std::int64_t{0};
  auto reserved_limit_mb = std::int64_t{0};
  auto solution = std::string{};
  for (const auto& row : database(sqlpp::select(db::tasks.timeLimitMs, db::tasks.memoryUsedLimitMb,
                                                db::tasks.memoryReservedLimitMb, db::tasks.solution,
                                                db::tasks.contentHash)
                                      .from(db::tasks)
                                      .where(db::tasks.id == std::string{task_id} &&
                                             db::tasks.version == static_cast<std::int64_t>(version)))) {
    solution = std::string{row.solution};
    time_limit_ms = static_cast<std::int64_t>(row.timeLimitMs);
    used_limit_mb = static_cast<std::int64_t>(row.memoryUsedLimitMb);
    reserved_limit_mb = static_cast<std::int64_t>(row.memoryReservedLimitMb);
    task = Certificate{.job_id = 0,
                       .task_id = std::string{task_id},
                       .task_version = version,
                       .outcome = VerificationOutcome::verified,
                       .commit_id = std::string{commit_id},
                       .checks = {},
                       .artefacts = {}};
  }
  if (!task) {
    return std::unexpected(Error::make(ErrorCode::not_found, "task " + std::string{task_id} + " version " +
                                                                 std::to_string(version) + " is not in the record"));
  }
  const auto task_root = std::filesystem::path{content_root} / "tasks" / std::string{task_id};
  const auto declared_reference = reference_path_of(task_root);
  if (!declared_reference) {
    return std::unexpected(declared_reference.error());
  }
  const auto& reference = *declared_reference;

  const auto examples = examples_of(task_root);
  if (!examples) {
    return std::unexpected(examples.error());
  }

  for (const auto& example : *examples) {
    const auto answer = run_program(solution, example.input, time_limit_ms);
    if (!answer) {
      return std::unexpected(Error::make(ErrorCode::invalid_argument,
                                         reference + " failed on " + example.input_path.string() + ": " +
                                             answer.error().message));
    }
    if (trimmed(*answer) != trimmed(example.expected)) {
      return std::unexpected(Error::make(
          ErrorCode::invalid_argument,
          reference + " does not match the answer expected on " + example.input_path.string() +
              ", so an author has to correct that file"));
    }
  }

  const auto half_time_ms = time_limit_ms / REFERENCE_MARGIN_DIVISOR;
  const auto half_used_kb = used_limit_mb * KB_PER_MB / REFERENCE_MARGIN_DIVISOR;
  const auto half_reserved_kb = reserved_limit_mb * KB_PER_MB / REFERENCE_MARGIN_DIVISOR;
  const auto measured = measure_program(solution, examples->back().input, time_limit_ms);
  if (!measured) {
    return std::unexpected(Error::make(ErrorCode::invalid_argument,
                                       reference + " could not be measured: " + measured.error().message));
  }
  auto exceeded = std::string{};
  const auto add = [&exceeded](std::string sentence) {
    exceeded += exceeded.empty() ? "" : "; ";
    exceeded += std::move(sentence);
  };
  if (measured->time_ms > half_time_ms) {
    add(std::to_string(measured->time_ms) + " ms is above the " + std::to_string(half_time_ms) + " ms half");
  }
  if (measured->peak_memory_kb > half_used_kb) {
    add(std::to_string(megabytes(measured->peak_memory_kb)) + " MB used is above the " +
        std::to_string(half_used_kb / KB_PER_MB) + " MB used half");
  }
  if (measured->peak_reserved_kb > half_reserved_kb) {
    add(std::to_string(megabytes(measured->peak_reserved_kb)) + " MB reserved is above the " +
        std::to_string(half_reserved_kb / KB_PER_MB) + " MB reserved half");
  }
  if (!exceeded.empty()) {
    return std::unexpected(Error::make(
        ErrorCode::invalid_argument,
        reference + " measures " + std::to_string(measured->time_ms) + " ms, " +
            std::to_string(megabytes(measured->peak_memory_kb)) + " MB used and " +
            std::to_string(megabytes(measured->peak_reserved_kb)) + " MB reserved on " +
            examples->back().input_path.string() + ", outside the declared " + std::to_string(time_limit_ms) +
            " ms, " + std::to_string(used_limit_mb) + " MB used and " + std::to_string(reserved_limit_mb) +
            " MB reserved limits (" + exceeded + ")"));
  }

  auto certificate = *task;
  certificate.checks.push_back("the reference " + reference + " passes every example the task ships (" +
                               std::to_string(examples->size()) + " of " + std::to_string(examples->size()) + ")");
  certificate.checks.push_back("the reference " + reference + " measures " + std::to_string(measured->time_ms) +
                               " ms, " + std::to_string(megabytes(measured->peak_memory_kb)) + " MB used and " +
                               std::to_string(megabytes(measured->peak_reserved_kb)) + " MB reserved at " +
                               examples->back().input_path.string() + ", inside the " +
                               std::to_string(half_time_ms) + " ms, " +
                               std::to_string(half_used_kb / KB_PER_MB) + " MB used and " +
                               std::to_string(half_reserved_kb / KB_PER_MB) + " MB reserved halves of the declared " +
                               std::to_string(time_limit_ms) + " ms, " + std::to_string(used_limit_mb) +
                               " MB used and " + std::to_string(reserved_limit_mb) + " MB reserved limits");
  certificate.checks.push_back(
      "the brute-force differential did not run: a hand-written task ships no brute-force program to compare against, "
      "so this certificate carries the checks a hand-written task can (ADR 0057 §2)");

  auto checks = std::string{"["};
  auto first = true;
  for (const auto& check : certificate.checks) {
    if (!first) {
      checks += ',';
    }
    first = false;
    checks += as_json_string(check);
  }
  checks += ']';

  try {
    database.execute_text(
        "INSERT INTO generation_certificates (job_id, task_id, task_version, outcome, checks, commit_id, evidence) "
        "VALUES (NULL, $1, $2, 'verified'::task_verification_status, $3::jsonb, $4, $5::jsonb)",
        {std::string{task_id}, static_cast<std::int64_t>(version), checks, std::string{commit_id},
         std::string{"{\"artefacts\": []}"}});
    database.execute_text("UPDATE tasks SET verification = 'verified' WHERE id = $1 AND version = $2",
                          {std::string{task_id}, static_cast<std::int64_t>(version)});
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }
  return certificate;
}

}  // namespace cms::generate
