#include <cms/judge/run.hpp>

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <utility>

namespace {

using cms::judge::SubmissionVerdict;

auto outcome(std::string status, bool is_killed = false) -> cms::judge::RunOutcome {
  auto value = cms::judge::RunOutcome{};
  value.status = std::move(status);
  value.is_killed = is_killed;
  return value;
}

auto sandbox_of(const cms::judge::RunOutcome& value) -> std::optional<SubmissionVerdict> {
  const auto mapped = cms::judge::sandbox_verdict(value);
  REQUIRE(mapped.has_value());
  return *mapped;
}

}  // namespace

TEST_CASE("a clean run leaves the verdict to the checker") {
  CHECK(!sandbox_of(outcome("OK")).has_value());
}

TEST_CASE("a sandbox status maps to the run's verdict") {
  CHECK(sandbox_of(outcome("TO")) == SubmissionVerdict::time_limit_exceeded);
  CHECK(sandbox_of(outcome("ML")) == SubmissionVerdict::memory_limit_exceeded);
  CHECK(sandbox_of(outcome("SG")) == SubmissionVerdict::runtime_error);
  CHECK(sandbox_of(outcome("RE")) == SubmissionVerdict::runtime_error);
  CHECK(sandbox_of(outcome("", true)) == SubmissionVerdict::time_limit_exceeded);
}

TEST_CASE("a sandbox that failed is a refusal, not a verdict") {
  CHECK(!cms::judge::sandbox_verdict(outcome("XX")).has_value());
  CHECK(!cms::judge::sandbox_verdict(outcome("")).has_value());
}

TEST_CASE("a compile failure is a compile_error verdict") {
  CHECK(cms::judge::compile_verdict(outcome("RE")) == SubmissionVerdict::compile_error);
  CHECK(cms::judge::compile_verdict(outcome("TO")) == SubmissionVerdict::compile_error);
  CHECK(cms::judge::compile_verdict(outcome("", true)) == SubmissionVerdict::compile_error);
  CHECK(!cms::judge::compile_verdict(outcome("OK")).has_value());
}

TEST_CASE("a wrong answer is never accepted") {
  const auto verdict = cms::judge::checker_verdict(cms::judge::Verdict::wrong_answer);
  CHECK(verdict == SubmissionVerdict::wrong_answer);
  CHECK(cms::judge::database_verdict(verdict) == "wrong_answer");
  CHECK(cms::judge::database_verdict(verdict) != "ok");
  CHECK(cms::judge::database_verdict(cms::judge::checker_verdict(cms::judge::Verdict::ok)) == "ok");
}

TEST_CASE("a failed checker is the checker's verdict, not a pass") {
  const auto verdict = cms::judge::checker_verdict(cms::judge::Verdict::check_failed);
  CHECK(verdict == SubmissionVerdict::checker_failed);
  CHECK(cms::judge::database_verdict(verdict) == "check_failed");
  CHECK(cms::judge::checker_verdict(cms::judge::Verdict::wrong_format) ==
        SubmissionVerdict::wrong_format);
  CHECK(cms::judge::checker_verdict(cms::judge::Verdict::partially_correct) ==
        SubmissionVerdict::partially_correct);
  CHECK(cms::judge::checker_verdict(cms::judge::Verdict::failed) == SubmissionVerdict::failed);
}

TEST_CASE("the database spelling covers every verdict") {
  CHECK(cms::judge::database_verdict(SubmissionVerdict::accepted) == "ok");
  CHECK(cms::judge::database_verdict(SubmissionVerdict::wrong_answer) == "wrong_answer");
  CHECK(cms::judge::database_verdict(SubmissionVerdict::wrong_format) == "wrong_format");
  CHECK(cms::judge::database_verdict(SubmissionVerdict::partially_correct) == "partially_correct");
  CHECK(cms::judge::database_verdict(SubmissionVerdict::compile_error) == "failed");
  CHECK(cms::judge::database_verdict(SubmissionVerdict::runtime_error) == "failed");
  CHECK(cms::judge::database_verdict(SubmissionVerdict::time_limit_exceeded) == "failed");
  CHECK(cms::judge::database_verdict(SubmissionVerdict::memory_limit_exceeded) == "failed");
  CHECK(cms::judge::database_verdict(SubmissionVerdict::checker_failed) == "check_failed");
  CHECK(cms::judge::database_verdict(SubmissionVerdict::failed) == "failed");
}
