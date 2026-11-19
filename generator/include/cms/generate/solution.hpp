#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <cms/error.hpp>
#include <cms/generate/model.hpp>
#include <cms/generate/specification.hpp>

namespace drogon::orm {
class DbClient;
}

namespace cms::store {
class ObjectStore;
}

namespace cms::generate {

/// @brief One program step 5 wrote: the reference (position 0) or a subtask's solution (its position).
struct WrittenSolution {
  std::int32_t position{0};
  std::string source;
  ModelCall call;
};

/// @brief What step 5 produces, before it is proven: the reference and one solution per subtask.
struct Step5Answer {
  WrittenSolution reference;
  std::vector<WrittenSolution> partials;
  /// The answer of the call that wrote the reference, kept so the call row can carry its own bytes.
  std::string reference_answer;
};

/// @brief One case the bounds admit, at the smallest size they name.
struct SmallCase {
  std::string input;
  std::int64_t n{0};
};

/// @brief The cases a subtask's solution must earn: at or below @p bound, and one above it.
struct SubtaskCases {
  std::vector<SmallCase> inside;
  std::vector<SmallCase> outside;
};

/// @brief The proof a partial earned: which cases it passed, and the first case outside that it failed.
struct PartialProof {
  std::int32_t position{0};
  std::int64_t inside_cases{0};
  bool failed_outside{false};
  std::string sentence;
};

/// @brief The cases a subtask's typed narrowing admits, drawn by the sampler (ADR 0040, 0041).
///
/// A case is only ever built from what the specification declares — the task's bounds and the subtask's narrowing —
/// never from a value the model chose, because a proof that rests on an invented case proves nothing about the task.
/// @return The cases, or a refusal naming the shape or narrowing the sampler cannot draw.
[[nodiscard]] auto admissible_cases(const Specification& specification, const SubtaskConstraint& subtask)
    -> Result<std::vector<SmallCase>>;

/// @brief The subtask's own cases (inside its narrowing) and one case outside it but inside the task's bounds.
/// @return The cases, or a refusal naming why the narrowing leaves no case outside it.
[[nodiscard]] auto subtask_cases(const Specification& specification, const SubtaskConstraint& subtask)
    -> Result<SubtaskCases>;

/// @brief Runs the reference and every partial, and returns the proof each partial earned.
///
/// The reference must agree with the oracle on every admissible case, token for token (ADR 0042); a disagreement is
/// refused by name. A partial must pass every case inside its subtask and fail at least one outside it.
[[nodiscard]] auto check_solutions(const Step5Answer& answer, const Specification& specification,
                                   std::string_view oracle_source, const std::vector<SmallCase>& cases,
                                   std::int64_t time_limit_ms) -> Result<std::vector<PartialProof>>;

/// @brief The prompt step 5 sends for the reference: the specification and the oracle, both as framed data.
[[nodiscard]] auto reference_prompt(const Specification& specification, std::string_view oracle_source)
    -> Result<std::string>;

/// @brief The prompt step 5 sends for one subtask's solution.
[[nodiscard]] auto partial_prompt(const Specification& specification, const SubtaskConstraint& subtask,
                                  std::string_view oracle_source) -> Result<std::string>;

/// @brief Reads the reference answer: one complete program, and nothing else.
[[nodiscard]] auto parse_reference_answer(std::string_view answer, const ModelCall& call)
    -> Result<Step5Answer>;

/// @brief Reads one subtask's answer and adds it to @p answer, refusing a subtask that already has one.
[[nodiscard]] auto parse_partial_answer(std::string_view answer, std::int32_t position, const ModelCall& call,
                                        Step5Answer& into) -> Result<void>;

/// @brief Runs step 5: one call for the reference, one per subtask, each parsed as it arrives.
///
/// A specification with no subtask is refused: the reference alone leaves every subtask's weight unmeasured.
[[nodiscard]] auto write_solutions(ModelClient& client, const Specification& specification,
                                   std::string_view oracle_source) -> Result<Step5Answer>;

/// @brief Where step 5's record goes: one artefact row per program, and one solution row citing each.
class SolutionSink {
 public:
  virtual ~SolutionSink() = default;

  /// @param job_id The generation job the rows belong to.
  /// @param answer What step 5 wrote.
  /// @param proofs The proof each partial earned, in position order.
  /// @return How many rows were written, or the reason the write stopped.
  [[nodiscard]] virtual auto record(std::int64_t job_id, const Step5Answer& answer,
                                    const std::vector<PartialProof>& proofs) -> Result<std::size_t> = 0;
};

/// @brief A sink that keeps what a step would have written, so a test needs no database.
class MemorySolutionSink final : public SolutionSink {
 public:
  [[nodiscard]] auto record(std::int64_t job_id, const Step5Answer& answer,
                            const std::vector<PartialProof>& proofs) -> Result<std::size_t> override;

  [[nodiscard]] auto job_id() const -> std::int64_t { return job_id_; }
  [[nodiscard]] auto proofs() const -> const std::vector<PartialProof>& { return proofs_; }

 private:
  std::int64_t job_id_{0};
  std::vector<PartialProof> proofs_;
};

/// @brief The sink the worker uses: the bytes to the store, then every row in one transaction.
class SqlSolutionSink final : public SolutionSink {
 public:
  SqlSolutionSink(::drogon::orm::DbClient& client, cms::store::ObjectStore& store);

  [[nodiscard]] auto record(std::int64_t job_id, const Step5Answer& answer,
                            const std::vector<PartialProof>& proofs) -> Result<std::size_t> override;

 private:
  ::drogon::orm::DbClient& client_;
  cms::store::ObjectStore& store_;
};

}  // namespace cms::generate
