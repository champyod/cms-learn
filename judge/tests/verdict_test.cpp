#include <cms/judge/verdict.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace {

auto checker_named(std::string_view name) -> std::string {
  return std::string{CMS_CHECKER_DIR} + "/" + std::string{name};
}

auto verdict_of(std::string_view name, std::string_view output) -> cms::judge::Verdict {
  const auto result =
      cms::judge::run_checker(checker_named(name), "", std::string{output}, "");
  REQUIRE(result.has_value());
  return *result;
}

}  // namespace

TEST_CASE("a verdict is read from the token testlib writes", "[verdict][unit]") {
  using cms::judge::Verdict;
  CHECK(cms::judge::parse_checker_output("ok ", 0) == Verdict::ok);
  CHECK(cms::judge::parse_checker_output("OK\n", 0) == Verdict::ok);
  CHECK(cms::judge::parse_checker_output("ok tokens match", 0) == Verdict::ok);
  CHECK(cms::judge::parse_checker_output("wrong answer 1st words differ", 0) ==
        Verdict::wrong_answer);
  CHECK(cms::judge::parse_checker_output("wrong output format Unexpected end of file", 0) ==
        Verdict::wrong_format);
  CHECK(cms::judge::parse_checker_output("points 50.0", 0) == Verdict::partially_correct);
  CHECK(cms::judge::parse_checker_output("FAIL cannot open the answer", 0) == Verdict::failed);
}

TEST_CASE("anything a checker says that is not a verdict is a failed check", "[verdict][unit]") {
  using cms::judge::Verdict;
  // The plan's falsification: a checker that says something unrecognised must not be read as an
  // acceptance, and neither must one that says nothing at all.
  CHECK(cms::judge::parse_checker_output("pass", 0) == Verdict::check_failed);
  CHECK(cms::judge::parse_checker_output("accepted", 0) == Verdict::check_failed);
  // Not testlib 0.9.41's wording: `_pe` writes "wrong output format", so this must not be guessed into a
  // verdict, however familiar the phrase looks.
  CHECK(cms::judge::parse_checker_output("presentation error", 0) == Verdict::check_failed);
  CHECK(cms::judge::parse_checker_output("", 0) == Verdict::check_failed);
  CHECK(cms::judge::parse_checker_output("   \n\t", 0) == Verdict::check_failed);
  CHECK(cms::judge::parse_checker_output("ok", 3) == Verdict::check_failed);
  CHECK(cms::judge::parse_checker_output("wrong answer", 1) == Verdict::check_failed);
}

TEST_CASE("the judge reports the verdict it read, and never invents one", "[verdict][unit]") {
  CHECK(verdict_of("ok-checker.sh", "") == cms::judge::Verdict::ok);
  CHECK(verdict_of("garbage-checker.sh", "") == cms::judge::Verdict::check_failed);
  CHECK(verdict_of("silent-checker.sh", "") == cms::judge::Verdict::check_failed);
  CHECK(verdict_of("crashing-checker.sh", "") == cms::judge::Verdict::check_failed);
  CHECK(verdict_of("garbage-checker.sh", "") != cms::judge::Verdict::ok);
}

TEST_CASE("a checker that does not exist is an error, not a verdict", "[verdict][unit]") {
  const auto missing = cms::judge::run_checker(checker_named("does-not-exist"), "", "", "");
  REQUIRE(!missing.has_value());
  CHECK(missing.error().code == cms::ErrorCode::unavailable);
}
