#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <cms/db/connection.hpp>
#include <cms/error.hpp>
#include <cms/generate/artefact.hpp>
#include <cms/generate/brief.hpp>
#include <cms/generate/candidates.hpp>
#include <cms/generate/claim.hpp>
#include <cms/generate/model.hpp>
#include <cms/generate/specification.hpp>

namespace cms::generate {

/// @brief What one worker run did, so its caller can say it.
struct PipelineReport {
  /// The job the worker claimed; zero when the queue held nothing claimable.
  std::int64_t job_id{0};
  /// The state the claim ended in: `awaiting-evidence`, or `failed`; empty when nothing was claimable.
  std::string state;
  /// The steps that ran, in order.
  std::vector<std::string> steps;
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

/// @brief Claims one queued job and drives it through steps 1 to 3 (ADR 0015).
///
/// The claim is `claim_job`'s: one lease, one attempt, and the worker's name beside it. Step 1 plans the candidates,
/// step 2 chooses one and records every candidate through @p candidates, step 3 derives the specification, checks the
/// oracle and records it through @p specifications. Every refusal ends the attempt rather than leaving the job
/// running: the refusal is written to @p artefacts as an inline row for the step that produced it (there is no error
/// column on the job), the claim ends `failed`, and the error carries the step's number in front of the step's own
/// words. A job that reaches the end of step 3 ends `awaiting-evidence`, which is the state a task's evidence waits in
/// until a certificate certifies it (ADR 0046).
///
/// @param client The model the deployer configured, or a recording of it.
/// @param database The worker's connection; the role behind it is cmslearn_generator, not the certifier's.
/// @param worker The name written beside the claim, so a run that dies can be traced to a person.
/// @param candidates Steps 1 and 2's record.
/// @param specifications Step 3's record.
/// @param artefacts Where a refusal is recorded when a step ends the run.
/// @return What ran and the state the claim ended in, or the refusal that ended it.
[[nodiscard]] auto run_pipeline(ModelClient& client, cms::db::connection& database, std::string_view worker,
                                CandidateSink& candidates, SpecificationSink& specifications,
                                ArtefactSink& artefacts) -> Result<PipelineReport>;

}  // namespace cms::generate
