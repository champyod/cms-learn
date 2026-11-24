#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <cms/error.hpp>
#include <cms/generate/candidate.hpp>
#include <cms/generate/model.hpp>
#include <cms/subtask.hpp>

namespace drogon::orm {
class DbClient;
}

namespace cms::store {
class ObjectStore;
}

namespace cms::generate {

/// @brief The task's bounds as this step names them: one shared shape, so the sampler, the bank and this step cannot
/// describe a different interval (ADR 0040, 0041).
using Bounds = cms::TaskBounds;

/// @brief One subtask's typed constraint: what makes its group smaller than the whole task (ADR 0033, 0041).
struct SubtaskConstraint {
  std::int32_t position{0};
  cms::SubtaskBounds bounds;
};

/// @brief Step 3's specification: what goes in, what comes out, the task's bounds, and the subtask narrowings.
struct Specification {
  std::string input;
  std::string output;
  cms::TaskBounds bounds;
  std::vector<SubtaskConstraint> subtasks;
};

/// @brief The time and the two memory limits, with the sentence that shows how they were computed.
///
/// ADR 0015 (interpretation of 2026-11-17): the limits are the task's, and a subtask carries a constraint instead.
/// ADR 0059: `used` is what the program may touch, `reserved` the address space it may map.
struct DerivedLimit {
  std::int64_t time_limit_ms{0};
  std::int64_t memory_used_limit_mb{0};
  std::int64_t memory_reserved_limit_mb{0};
  std::string derivation;
};

/// @brief What step 3 produces: the specification, its derived limit, and the brute-force oracle's source.
struct Step3Answer {
  Specification specification;
  DerivedLimit limit;
  std::string oracle_source;
  /// The model's answer as it arrived, which is what the step's call row carries: the parsed fields are a reading
  /// of it, and a reader who disputes the reading needs the bytes it was read from.
  std::string answer_json;
  ModelCall call;
};

/// The band a task's own columns allow, so a derived limit that lands outside it is refused rather than stored
/// (deploy/schema/content.sql:39-40, and the same two CHECKs on generation_specifications).
inline constexpr std::int64_t TIME_LIMIT_FLOOR_MS = 100;
inline constexpr std::int64_t TIME_LIMIT_CEILING_MS = 20000;
inline constexpr std::int64_t MEMORY_LIMIT_FLOOR_MB = 16;
inline constexpr std::int64_t MEMORY_LIMIT_CEILING_MB = 1024;

/// The machine fact the derivation is made of: a competitive-programming judge allows roughly this many simple
/// operations per millisecond, so a plan's complexity at a bound converts to a time limit without a human.
inline constexpr std::int64_t OPERATIONS_PER_MILLISECOND = 100'000;

/// @brief The operations a complexity names at @p n, or a refusal naming the form this step cannot read.
[[nodiscard]] auto operations_at(std::string_view complexity, std::int64_t n) -> Result<double>;

/// @brief Derives the one limit pair from @p complexity at @p bounds, and states the derivation.
///
/// @return The pair and its sentence, or a refusal naming the reason: no bound to derive from, a complexity this
/// step cannot read, or a derivation that lands outside the band.
[[nodiscard]] auto derive_limit(std::string_view complexity, const cms::TaskBounds& bounds) -> Result<DerivedLimit>;

/// @brief The specification as step 3's row stores it, written and read by one definition in `specification_sink.cpp`.
///
/// The writer and @ref read_specification share one translation unit and one set of key names, so the shape a row
/// carries cannot drift from the shape the certifier reads: a change to it is a build error in both, never a reader
/// that quietly ignores a fact its writer added (ADR 0046).
[[nodiscard]] auto specification_json(const Specification& specification) -> std::string;

/// @brief Reads the JSON @ref specification_json wrote, refusing a specification that lost a fact or names a shape
/// this pipeline cannot build.
[[nodiscard]] auto read_specification(std::string_view json) -> Result<Specification>;

/// @brief Reads one specification answer: the four facts, the bounds, the subtasks and the oracle's source.
[[nodiscard]] auto parse_specification_answer(std::string_view answer, const ModelCall& call)
    -> Result<Step3Answer>;

/// @brief The prompt step 3 sends: the chosen plan as framed data, and the JSON shape of the answer.
[[nodiscard]] auto specification_prompt(const CandidatePlan& chosen) -> Result<std::string>;

/// @brief Runs step 3: one model call, the specification read back, the limit derived from the plan's complexity.
[[nodiscard]] auto specify_step3(ModelClient& client, const CandidatePlan& chosen) -> Result<Step3Answer>;

/// @brief What the oracle check proved: the program compiled, ran on the smallest case, and answered.
struct OracleCheck {
  std::string smallest_input;
  std::string answer;
};

/// @brief Compiles @p source and runs it on @p input, refusing a program that does not build or answer.
///
/// The refusal carries only the reason, so a caller can name which program failed: a step that runs a reference, a
/// subtask's solution and an oracle would otherwise produce three sentences that cannot be told apart.
[[nodiscard]] auto run_program(std::string_view source, std::string_view input, std::int64_t time_limit_ms)
    -> Result<std::string>;

/// @brief One run of a program that was compiled once: whether it finished, and its output or the reason it stopped.
struct ProgramRun {
  bool is_ok{false};
  std::string output;
};

/// @brief Compiles @p source once and runs it for every input, in order, so proving many cases costs one compile.
///
/// An error is returned only when the program does not compile: a run that fails is a result, because a caller
/// proving that a validator refuses bad input expects most of these runs to fail.
[[nodiscard]] auto run_program_over(std::string_view source, const std::vector<std::string>& inputs,
                                    std::int64_t time_limit_ms) -> Result<std::vector<ProgramRun>>;

/// @brief Compiles @p source and runs it on @p smallest_input, refusing a program that does not build or answer.
[[nodiscard]] auto check_oracle(std::string_view source, std::string_view smallest_input,
                                std::int64_t time_limit_ms) -> Result<OracleCheck>;

/// @brief What one run of a program cost, which the half-limit margin of ADR 0048 is measured against.
struct ProgramMeasurement {
  std::int64_t time_ms{0};
  /// Peak resident memory the run touched, in kilobytes: what the used limit counts.
  std::int64_t peak_memory_kb{0};
  /// Peak virtual address space the run mapped, in kilobytes: what the reserved limit counts (`RLIMIT_AS`).
  std::int64_t peak_reserved_kb{0};
};

/// @brief Runs @p source on @p input and measures the run itself, refusing a program that does not build or answer.
///
/// The measurement covers the program's own run, never the compile: a reference that needs a long build is not a
/// reference that is slow for a student, and the limit is the student's.
[[nodiscard]] auto measure_program(std::string_view source, std::string_view input, std::int64_t time_limit_ms)
    -> Result<ProgramMeasurement>;

/// @brief Where step 3's record goes: two artefact rows, the store's bytes, and one specification row.
class SpecificationSink {
 public:
  virtual ~SpecificationSink() = default;

  /// @param job_id The generation job the rows belong to.
  /// @param answer What step 3 produced, already checked.
  /// @param oracle The oracle's own check, which the answer must have passed.
  /// @return The oracle's digest, or the reason the write stopped.
  [[nodiscard]] virtual auto record(std::int64_t job_id, const Step3Answer& answer,
                                    const OracleCheck& oracle) -> Result<std::string> = 0;
};

/// @brief A sink that keeps what a step would have written, so a test needs no database.
class MemorySpecificationSink final : public SpecificationSink {
 public:
  [[nodiscard]] auto record(std::int64_t job_id, const Step3Answer& answer,
                            const OracleCheck& oracle) -> Result<std::string> override;

  [[nodiscard]] auto job_id() const -> std::int64_t { return job_id_; }
  [[nodiscard]] auto answer() const -> const Step3Answer& { return answer_; }
  [[nodiscard]] auto oracle() const -> const OracleCheck& { return oracle_; }

 private:
  std::int64_t job_id_{0};
  Step3Answer answer_;
  OracleCheck oracle_;
};

/// @brief The sink the worker uses: the oracle's bytes to the store, then every row in one transaction.
class SqlSpecificationSink final : public SpecificationSink {
 public:
  SqlSpecificationSink(::drogon::orm::DbClient& client, cms::store::ObjectStore& store);

  [[nodiscard]] auto record(std::int64_t job_id, const Step3Answer& answer,
                            const OracleCheck& oracle) -> Result<std::string> override;

 private:
  ::drogon::orm::DbClient& client_;
  cms::store::ObjectStore& store_;
};

}  // namespace cms::generate
