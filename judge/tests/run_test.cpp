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

TEST_CASE("a memory verdict names which of the two limits was exceeded") {
  auto used_over = outcome("OK");
  used_over.meta["max-rss"] = "307200";
  const auto used = cms::judge::memory_verdict(used_over, 256 * 1024, 512 * 1024);
  REQUIRE(used.has_value());
  REQUIRE(used->has_value());
  CHECK((*used)->verdict == SubmissionVerdict::memory_limit_exceeded);
  CHECK((*used)->detail.find("used memory") != std::string::npos);
  CHECK((*used)->detail.find("300 MB") != std::string::npos);
  CHECK((*used)->detail.find("reserved") == std::string::npos);

  const auto reserved = cms::judge::memory_verdict(outcome("ML"), 256 * 1024, 512 * 1024);
  REQUIRE(reserved.has_value());
  REQUIRE(reserved->has_value());
  CHECK((*reserved)->verdict == SubmissionVerdict::memory_limit_exceeded);
  CHECK((*reserved)->detail.find("reserved memory") != std::string::npos);
  CHECK((*reserved)->detail.find("512 MB") != std::string::npos);
  CHECK((*reserved)->detail.find("used") == std::string::npos);
}

TEST_CASE("a run inside both memory limits is left to the checker") {
  auto inside = outcome("OK");
  inside.meta["max-rss"] = "1024";
  const auto verdict = cms::judge::memory_verdict(inside, 256 * 1024, 512 * 1024);
  REQUIRE(verdict.has_value());
  CHECK(!verdict->has_value());
  // Another status owns its own verdict; memory has nothing to add.
  const auto timeout = cms::judge::memory_verdict(outcome("TO"), 256 * 1024, 512 * 1024);
  REQUIRE(timeout.has_value());
  CHECK(!timeout->has_value());
}

TEST_CASE("a clean run with no readable peak memory is a refusal, never a pass") {
  const auto absent = cms::judge::memory_verdict(outcome("OK"), 256 * 1024, 512 * 1024);
  REQUIRE(!absent.has_value());
  CHECK(absent.error().message.find("no peak resident memory") != std::string::npos);

  auto garbage = outcome("OK");
  garbage.meta["max-rss"] = "lots";
  const auto unreadable = cms::judge::memory_verdict(garbage, 256 * 1024, 512 * 1024);
  REQUIRE(!unreadable.has_value());
  CHECK(unreadable.error().message.find("cannot read") != std::string::npos);
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
