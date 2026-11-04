#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <cms/error.hpp>
#include <cms/judge/supervisor.hpp>
#include <cms/judge/verdict.hpp>

namespace cms::judge {

/// @brief The C++ standard a submission is compiled with when its task declares none.
///
/// No task field declares a standard today; this is the named default the judge uses, and the same value
/// the reference examples are built against.
inline constexpr std::string_view DEFAULT_CPP_STANDARD = "c++17";

/// @brief Every verdict a judged submission can reach, including the ones the database spells differently.
enum class SubmissionVerdict : std::uint8_t {
  accepted,
  wrong_answer,
  wrong_format,
  partially_correct,
  compile_error,
  runtime_error,
  time_limit_exceeded,
  memory_limit_exceeded,
  checker_failed,
  failed,
};

/// @brief One judged submission: what was decided, and the sentence that explains it.
struct Judgement {
  SubmissionVerdict verdict{SubmissionVerdict::failed};
  std::string detail;
};

/// @brief How a judgement is spelled in `submissions.verdict`, which has no compile or runtime names.
[[nodiscard]] auto database_verdict(SubmissionVerdict verdict) -> std::string_view;

/// @brief How a verdict is spelled in the runner's output and in `submissions.verdict_detail`.
[[nodiscard]] auto submission_verdict_name(SubmissionVerdict verdict) -> std::string_view;

/// @brief The verdict a sandbox report means, or nothing when the program finished and the checker decides.
/// @return Nothing for a clean `OK`; a verdict for TO, ML, SG and RE; or an error when the sandbox itself
/// failed, which leaves the submission claimable rather than giving it a verdict.
[[nodiscard]] auto sandbox_verdict(const RunOutcome& outcome) -> Result<std::optional<SubmissionVerdict>>;

/// @brief The verdict a compile run means; nothing when the compiler reported success.
[[nodiscard]] auto compile_verdict(const RunOutcome& outcome) -> std::optional<SubmissionVerdict>;

/// @brief The verdict a checker's answer becomes; only `ok` is an acceptance.
[[nodiscard]] auto checker_verdict(Verdict verdict) -> SubmissionVerdict;

/// @brief One case: what the program reads, and the answer the checker compares against.
struct Case {
  std::string input;
  std::string expected_output;
};

/// @brief One submission's task, as the judge needs it to run the submission.
struct TaskRun {
  std::int64_t time_limit_ms{1000};
  std::int64_t memory_limit_kb{262144};
  std::string checker;
  std::string cpp_standard{std::string{DEFAULT_CPP_STANDARD}};
  std::vector<Case> cases;
};

/// @brief Where and with what one submission is run.
struct RunSettings {
  std::string isolate;
  std::string compiler{"g++"};
  std::string box_id{"0"};
  std::int64_t compile_time_limit_ms{60000};
  std::int64_t compile_memory_kb{1048576};
};

/// @brief Compiles the source in a sandbox, runs every case, and checks each one.
///
/// @param settings The sandbox, compiler and box to use.
/// @param task The task's limits, checker and cases.
/// @param source The submission's C++ source, as the learner wrote it.
/// @return The judgement, or an error when the sandbox or compiler could not run at all. A compile
/// failure is a judgement, not an error, and a task with no cases is a refusal.
[[nodiscard]] Result<Judgement> judge_submission(const RunSettings& settings, const TaskRun& task,
                                                 std::string_view source);

}  // namespace cms::judge
