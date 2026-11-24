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

/// @brief One program step 6 wrote: the generator (seeded), the checker (the comparison rule) or the validator
/// (what an admissible input is).
struct TestProgram {
  std::string role;
  std::string source;
  ModelCall call;
};

/// @brief One case: the input the generator printed for a seed, and the answer the oracle gave it.
struct GeneratedCase {
  std::int32_t subtask_position{0};
  std::int64_t seed{0};
  std::int64_t n{0};
  std::string input;
  std::string expected;
};

/// @brief What step 6 produces: the three programs and the cases each subtask needs.
struct Step6Answer {
  TestProgram generator;
  TestProgram checker;
  TestProgram validator;
  std::vector<GeneratedCase> cases;
};

/// @brief The score one program earned: how many cases the checker accepted, and which subtask they were.
struct Score {
  std::int32_t position{0};
  std::int64_t accepted{0};
  std::int64_t total{0};
  std::string sentence;
};

/// @brief Reads a program answer: a complete C++ program, nothing else.
[[nodiscard]] auto parse_test_program(std::string_view answer, std::string_view role, const ModelCall& call)
    -> Result<TestProgram>;

/// @brief The prompt that asks for the generator: the bounds, the shape, and the seed contract.
[[nodiscard]] auto generator_prompt(const Specification& specification) -> Result<std::string>;

/// @brief The prompt that asks for the checker: the comparison rule, spelled out.
[[nodiscard]] auto checker_prompt(const Specification& specification) -> Result<std::string>;

/// @brief The prompt that asks for the validator: the input shape and the bounds it must refuse outside.
[[nodiscard]] auto validator_prompt(const Specification& specification) -> Result<std::string>;

/// @brief The validator: refuses a generated input that is not the declared shape within the declared bounds.
///
/// @return Nothing when the input is admissible, or a refusal naming the seed, n and the value it met.
[[nodiscard]] auto validate_generated(const Specification& specification, std::int64_t seed, std::int64_t n,
                                      std::string_view input) -> Result<void>;

/// @brief Runs the generator twice with the same stdin and refuses when the two runs differ.
[[nodiscard]] auto check_deterministic(std::string_view generator, std::string_view input, std::int64_t time_limit_ms)
    -> Result<std::string>;

/// @brief Runs the checker on (expected, answer) and returns "ok" or the checker's refusal.
[[nodiscard]] auto check_answer(std::string_view checker, std::string_view expected, std::string_view answer,
                                std::int64_t time_limit_ms) -> Result<std::string>;

/// @brief The seeds and sizes one subtask's cases are generated at, drawn by the sampler from its narrowing.
///
/// The generator that writes a case's bytes is step 6's own program (ADR 0047); which sizes a group needs is the
/// sampler's decision (ADR 0040), so the two steps cannot disagree about a group's shape.
[[nodiscard]] auto subtask_seeds(const Specification& specification, const SubtaskConstraint& subtask)
    -> Result<std::vector<std::pair<std::int64_t, std::int64_t>>>;

/// @brief Builds the cases per subtask, running the generator with each seed and the oracle for the answer.
[[nodiscard]] auto build_cases(const Specification& specification, std::string_view generator,
                               std::string_view oracle, std::int64_t time_limit_ms) -> Result<std::vector<GeneratedCase>>;

/// @brief Measures the score of @p solutions through the checker, and proves the shape the record demands.
///
/// The reference (position 0) must be accepted on every case; each partial must be accepted on its own subtask's
/// cases and rejected on at least one case outside them.
[[nodiscard]] auto score_solutions(const Step6Answer& answer, std::string_view reference,
                                   const std::vector<std::pair<std::int32_t, std::string>>& partials,
                                   std::int64_t time_limit_ms) -> Result<std::vector<Score>>;

/// @brief What step 6 produced and what its programs earned: the sink records both, so a caller that received only the
/// scores could not write the step.
struct TestPlanResult {
  Step6Answer answer;
  std::vector<Score> scores;
};

/// @brief Runs step 6: three calls, the cases built from the generator, and both proofs.
[[nodiscard]] auto write_test_plan(ModelClient& client, const Specification& specification,
                                   std::string_view oracle, std::string_view reference,
                                   const std::vector<std::pair<std::int32_t, std::string>>& partials,
                                   std::int64_t time_limit_ms) -> Result<TestPlanResult>;

/// @brief Where step 6's record goes: one artefact row per program and per subtask's cases, cited by its rows.
class TestPlanSink {
 public:
  virtual ~TestPlanSink() = default;

  [[nodiscard]] virtual auto record(std::int64_t job_id, const Step6Answer& answer,
                                    const std::vector<Score>& scores) -> Result<std::size_t> = 0;
};

/// @brief A sink that keeps what a step would have written, so a test needs no database.
class MemoryTestPlanSink final : public TestPlanSink {
 public:
  [[nodiscard]] auto record(std::int64_t job_id, const Step6Answer& answer,
                            const std::vector<Score>& scores) -> Result<std::size_t> override;

  [[nodiscard]] auto job_id() const -> std::int64_t { return job_id_; }
  [[nodiscard]] auto answer() const -> const Step6Answer& { return answer_; }

 private:
  std::int64_t job_id_{0};
  Step6Answer answer_;
};

/// @brief The sink the worker uses: the bytes to the store, then every row in one transaction.
class SqlTestPlanSink final : public TestPlanSink {
 public:
  SqlTestPlanSink(::drogon::orm::DbClient& client, cms::store::ObjectStore& store);

  [[nodiscard]] auto record(std::int64_t job_id, const Step6Answer& answer,
                            const std::vector<Score>& scores) -> Result<std::size_t> override;

 private:
  ::drogon::orm::DbClient& client_;
  cms::store::ObjectStore& store_;
};

}  // namespace cms::generate
