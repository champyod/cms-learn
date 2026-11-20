#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <cms/db/connection.hpp>
#include <cms/error.hpp>
#include <cms/generate/artefact.hpp>
#include <cms/generate/brief.hpp>
#include <cms/generate/candidates.hpp>
#include <cms/generate/certify.hpp>
#include <cms/generate/claim.hpp>
#include <cms/generate/model.hpp>
#include <cms/generate/solution.hpp>
#include <cms/generate/specification.hpp>
#include <cms/generate/statement.hpp>
#include <cms/generate/test_plan.hpp>
#include <cms/object_store.hpp>

namespace cms::generate {

/// @brief What one worker run did, so its caller can say it.
struct PipelineReport {
  /// The job the worker claimed; zero when the queue held nothing claimable.
  std::int64_t job_id{0};
  /// The state the claim ended in: `done` or `failed`; empty when nothing was claimable.
  std::string state;
  /// The steps that ran, in order.
  std::vector<std::string> steps;
};

/// @brief The certifier the runner asks for its ending.
///
/// It holds the connection of the role that may decide one: cmslearn_certifier owns INSERT on generation_certificates
/// and UPDATE (verification) on tasks, and the generator's role holds neither. A certifier that shared the worker's
/// connection would be a privilege escalation at runtime that those grants exist to prevent, so this is an interface
/// the caller implements on its own connection rather than a function the runner calls on the worker's.
class Certifier {
 public:
  virtual ~Certifier() = default;

  /// @param job_id The job whose evidence is certified.
  /// @return The certificate it wrote, or the reason it wrote none.
  [[nodiscard]] virtual auto certify(std::int64_t job_id) -> Result<Certificate> = 0;
};

/// @brief The certifier the worker uses: `certify_job` in one transaction of its own.
///
/// The certificate row and the task's verification commit together, which is what ADR 0046 requires of one decisive
/// step: they are written on this connection, in this transaction, so no reader can see a certificate that did not move
/// the task or a task that moved without one.
class SqlCertifier final : public Certifier {
 public:
  /// @param client The pool of the certifier's own role; the transaction this writes in opens on it.
  /// @param store The store every recorded artefact is read back through.
  /// @param commit_id The commit the evidence was produced from, recorded so a later reader knows what was checked.
  SqlCertifier(::drogon::orm::DbClient& client, const cms::store::ObjectStore& store, std::string_view commit_id);

  [[nodiscard]] auto certify(std::int64_t job_id) -> Result<Certificate> override;

 private:
  ::drogon::orm::DbClient& client_;
  const cms::store::ObjectStore& store_;
  std::string commit_id_;
};

/// @brief Where the runner writes and what it reads back.
struct PipelinePorts {
  /// The pool the worker's connection was built from; the artefact reader takes it, so both views are named once.
  ::drogon::orm::DbClient& client;
  /// The store every cited artefact is read back through.
  const cms::store::ObjectStore& store;
  CandidateSink& candidates;
  SpecificationSink& specifications;
  ArtefactSink& artefacts;
  StatementSink& statements;
  SolutionSink& solutions;
  TestPlanSink& test_plans;
  Certifier& certifier;
};

/// @brief What the runner needs that is neither a model call nor a row.
struct RunnerFacts {
  /// The vocabulary the technique's four names are read from: the content root's `topics.json`.
  std::filesystem::path vocabulary;
  /// The locales the task ships, named as the prompt needs them. Empty is refused at step 4 rather than guessed from a
  /// tag: ADR 0031 §2 keeps a language's name out of the step, so it can only arrive as data.
  std::vector<LocaleDeclaration> locales;
};

/// @brief The brief step 1 reads, assembled from the job row and the task it belongs to.
///
/// The teacher's free text and the admin's one line are the job's own columns (ADR 0036 §2). The other rows come from
/// the task the job names: its difficulty, its topics, and the locales it ships a statement in. The technique is the
/// task's first topic, which is not a guess: the content gate requires a technique to be one of the task's topics, so
/// the first is a topic the task already declares. A task with no topic cannot name one and is refused by name.
///
/// @param database The worker's connection.
/// @param claim The job the worker holds.
/// @return The brief, or a refusal naming the job, the task or the empty list it could not read.
[[nodiscard]] auto load_brief(cms::db::connection& database, const JobClaim& claim) -> Result<Brief>;

/// @brief Runs steps 4 to 7 for a job whose step 3 is recorded, and ends the claim on the certificate.
///
/// Step 4 writes the statement for each declared locale once both leak checks pass, step 5 writes the reference and
/// every subtask's solution and proves each partial on the cases its narrowing admits, and step 6 writes the generator,
/// the checker and each subtask's cases. Each step reads the artefact of the step before it back through the digest its
/// row carries before it records anything, so no output is recorded without the step that produced it. Step 7 asks the
/// certifier, and the claim ends `done` only when the certificate says verified; anything else ends it `failed` with the
/// certificate's own checks recorded against the job (ADR 0046, ADR 0050).
///
/// @param client The model the deployer configured, or a recording of it.
/// @param database The worker's connection.
/// @param worker The name written beside the claim.
/// @param claim The job the worker holds.
/// @param ports The sinks each step already owns, the store, and the certifier.
/// @param facts The vocabulary path and the declared locales.
/// @return What ran and the state the claim ended in, or the refusal that ended it.
[[nodiscard]] auto run_second_half(ModelClient& client, cms::db::connection& database, std::string_view worker,
                                   const JobClaim& claim, PipelinePorts ports, const RunnerFacts& facts)
    -> Result<PipelineReport>;

/// @brief Claims one queued job and drives it through all seven steps (ADR 0015).
///
/// The claim is `claim_job`'s: one lease, one attempt, and the worker's name beside it. Steps 1 to 3 run here and steps
/// 4 to 7 in run_second_half, which ends the claim. Every refusal ends the attempt rather than leaving the job running:
/// the refusal is written to the artefacts sink as an inline row for the step that produced it (there is no error
/// column on the job), the claim ends `failed`, and the error carries the step's number in front of the step's own
/// words.
[[nodiscard]] auto run_pipeline(ModelClient& client, cms::db::connection& database, std::string_view worker,
                                PipelinePorts ports, const RunnerFacts& facts) -> Result<PipelineReport>;

}  // namespace cms::generate
