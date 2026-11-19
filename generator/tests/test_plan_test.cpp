// Step 6: the generator, the checker and the cases. Every proof here is a program run twice or against a planted
// wrong answer, never a sentence: a deterministic generator, a checker that rejects, and a score that is measured.

#include <cms/generate/model.hpp>
#include <cms/generate/specification.hpp>
#include <cms/generate/test_plan.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <sqlpp23/sqlpp23.h>

#include <cms/db/connection.hpp>
#include <cms/db/tables.hpp>
#include <cms/object_store.hpp>

#include "live_database.hpp"
#include "memory_store.hpp"
#include "pipeline_fixture.hpp"

namespace {

using cms::generate::ModelCall;
using cms::generate::Specification;
using cms::generate::SubtaskConstraint;

/// A generator implementing the contract: stdin "<seed> <n>", stdout "n v1..vn".
constexpr std::string_view CONSTANT_GENERATOR = R"(#include <iostream>
int main() {
  long long seed = 0;
  long long n = 0;
  std::cin >> seed >> n;
  std::cout << n << "\n";
  for (long long index = 0; index < n; ++index) {
    std::cout << 3 << (index + 1 == n ? '\n' : ' ');
  }
  return 0;
}
)";

/// A generator that ignores its seed: it must be refused by the determinism check only if it varies, so this one is
/// deterministic and admissible; the non-deterministic one below is the refusal.
constexpr std::string_view CLOCK_GENERATOR = R"(#include <iostream>
#include <unistd.h>
int main() {
  long long seed = 0;
  long long n = 0;
  std::cin >> seed >> n;
  std::cout << n << "\n";
  for (long long index = 0; index < n; ++index) {
    std::cout << (index == 0 ? static_cast<long long>(getpid() % 7) : 3) << (index + 1 == n ? '\n' : ' ');
  }
  return 0;
}
)";

/// A checker implementing the contract: argv expected, answer; exit 0 when the tokens agree.
constexpr std::string_view TOKEN_CHECKER = R"(#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
int main(int argc, char** argv) {
  if (argc < 3) {
    return 2;
  }
  std::ifstream expected_file(argv[1]);
  std::ifstream answer_file(argv[2]);
  std::string expected_token;
  std::string answer_token;
  while (expected_file >> expected_token && answer_file >> answer_token) {
    if (expected_token != answer_token) {
      return 1;
    }
  }
  if (expected_file >> expected_token) {
    return 1;
  }
  if (answer_file >> answer_token) {
    return 1;
  }
  return 0;
}
)";

/// The most dangerous artefact in the pipeline: a checker that says yes to everything.
constexpr std::string_view ALWAYS_OK_CHECKER = R"(int main() { return 0; }
)";

/// A checker that compares whole files: right answers pass, and a wrong one passes as soon as one token matches.
constexpr std::string_view LOOSE_CHECKER = R"(#include <fstream>
#include <iostream>
#include <string>
int main(int argc, char** argv) {
  if (argc < 3) {
    return 2;
  }
  std::ifstream expected_file(argv[1]);
  std::ifstream answer_file(argv[2]);
  std::string expected;
  std::string answer;
  expected_file >> expected;
  answer_file >> answer;
  return expected.empty() ? 1 : 0;
}
)";

auto specification() -> Specification {
  return Specification{.input = "n, then n integers",
                       .output = "their sum",
                       .bounds = {.min_n = 1, .max_n = 200000, .value_min = 3, .value_max = 3},
                       .subtasks = {cms::generate::SubtaskConstraint{
                                        .position = 1, .bounds = cms::SubtaskBounds{.max_n = 100}},
                                    cms::generate::SubtaskConstraint{
                                        .position = 2, .bounds = cms::SubtaskBounds{.max_n = 200000}}}};
}

auto call(std::int32_t index) -> ModelCall {
  return ModelCall{.model_id = "tests", .prompt_version = "tests-v1", .seed = 9, .call_index = index};
}

auto answer_with(std::string_view generator = CONSTANT_GENERATOR, std::string_view checker = TOKEN_CHECKER)
    -> cms::generate::Step6Answer {
  return cms::generate::Step6Answer{
      .generator = {.role = "generator", .source = std::string{generator}, .call = call(0)},
      .checker = {.role = "checker", .source = std::string{checker}, .call = call(1)},
      .cases = {}};
}

constexpr std::string_view SUM_REFERENCE = R"(#include <iostream>
int main() {
  long long n = 0;
  std::cin >> n;
  long long total = 0;
  for (long long index = 0; index < n; ++index) {
    long long value = 0;
    std::cin >> value;
    total += value;
  }
  std::cout << total << "\n";
  return 0;
}
)";

/// The two partials the scoring cases use: each right inside its own subtask and wrong above it.
auto partials() -> std::vector<std::pair<std::int32_t, std::string>> {
  return {{1, R"(#include <iostream>
int main() {
  long long n = 0;
  std::cin >> n;
  long long total = 0;
  for (long long index = 0; index < n; ++index) {
    long long value = 0;
    std::cin >> value;
    total += value;
  }
  std::cout << (n <= 100 ? total : 0) << "\n";
  return 0;
})"},
          {2, R"(#include <iostream>
int main() {
  long long n = 0;
  std::cin >> n;
  long long total = 0;
  for (long long index = 0; index < n; ++index) {
    long long value = 0;
    std::cin >> value;
    total += value;
  }
  std::cout << (n <= 100000 ? total : 0) << "\n";
  return 0;
})"}};
}

}  // namespace

TEST_CASE("the generator is deterministic: the same seed twice prints the same bytes", "[generate]") {
  const auto checked = cms::generate::check_deterministic(CONSTANT_GENERATOR, "7 3\n", 1000);

  REQUIRE(checked.has_value());
  CHECK(*checked == "3\n3 3 3\n");
}

TEST_CASE("a generator that answers differently twice is refused by name", "[generate]") {
  const auto checked = cms::generate::check_deterministic(CLOCK_GENERATOR, "7 3\n", 1000);

  REQUIRE(!checked.has_value());
  CHECK(checked.error().message.find("not deterministic") != std::string::npos);
  CHECK(checked.error().message.find("two different outputs") != std::string::npos);
}

TEST_CASE("the validator refuses an input outside the declared bounds, naming the seed and the value", "[generate]") {
  const auto spec = specification();
  const auto too_large = cms::generate::validate_generated(spec, 4, 2, "2\n3 99\n");
  REQUIRE(!too_large.has_value());
  CHECK(too_large.error().message.find("seed 4") != std::string::npos);
  CHECK(too_large.error().message.find("99") != std::string::npos);
  CHECK(too_large.error().message.find("outside the declared values 3..3") != std::string::npos);

  const auto wrong_count = cms::generate::validate_generated(spec, 5, 3, "3\n3 3\n");
  REQUIRE(!wrong_count.has_value());
  CHECK(wrong_count.error().message.find("2 of 3 values") != std::string::npos);
}

TEST_CASE("the checker accepts the expected answer and rejects a planted wrong one", "[generate]") {
  const auto right = cms::generate::check_answer(TOKEN_CHECKER, "6\n", "6\n", 1000);
  REQUIRE(right.has_value());
  CHECK(*right == "ok");

  const auto wrong = cms::generate::check_answer(TOKEN_CHECKER, "6\n", "5\n", 1000);
  REQUIRE(wrong.has_value());
  CHECK(*wrong == "wrong");
}

TEST_CASE("a checker that accepts everything is refused by name", "[generate]") {
  const auto answer = answer_with(CONSTANT_GENERATOR, ALWAYS_OK_CHECKER);
  const auto cases = cms::generate::build_cases(specification(), CONSTANT_GENERATOR, SUM_REFERENCE, 1000);
  REQUIRE(cases.has_value());
  auto with_cases = answer;
  with_cases.cases = *cases;

  const auto scores = cms::generate::score_solutions(with_cases, SUM_REFERENCE, {{1, std::string{SUM_REFERENCE}}}, 1000);

  REQUIRE(!scores.has_value());
  CHECK(scores.error().message.find("accepts a planted wrong answer") != std::string::npos);
  CHECK(scores.error().message.find("not a checker") != std::string::npos);
}

TEST_CASE("a checker that compares whole files instead of answers is caught by the planted wrong one", "[generate]") {
  const auto answer = answer_with(CONSTANT_GENERATOR, LOOSE_CHECKER);
  const auto cases = cms::generate::build_cases(specification(), CONSTANT_GENERATOR, SUM_REFERENCE, 1000);
  REQUIRE(cases.has_value());
  auto with_cases = answer;
  with_cases.cases = *cases;

  const auto scores = cms::generate::score_solutions(with_cases, SUM_REFERENCE, {}, 1000);

  REQUIRE(!scores.has_value());
  CHECK(scores.error().message.find("accepts a planted wrong answer") != std::string::npos);
}

TEST_CASE("the reference scores full marks and each partial earns exactly its own subtask", "[generate]") {
  const auto cases = cms::generate::build_cases(specification(), CONSTANT_GENERATOR, SUM_REFERENCE, 1000);
  REQUIRE(cases.has_value());
  auto answer = answer_with();
  answer.cases = *cases;

  const auto scores = cms::generate::score_solutions(answer, SUM_REFERENCE, partials(), 1000);

  if (!scores) {
    FAIL("scoring refused: " + scores.error().message);
  }
  REQUIRE(scores.has_value());
  REQUIRE(scores->size() == 3);
  CHECK(scores->front().position == 0);
  CHECK(scores->front().accepted == scores->front().total);
  CHECK(scores->front().sentence.find("accepted on every case") != std::string::npos);
  CHECK(scores->at(1).sentence.find("subtask 1: accepted") != std::string::npos);
  CHECK(scores->at(2).sentence.find("subtask 2: accepted") != std::string::npos);
}

TEST_CASE("a partial accepted everywhere is refused: its points are not measured", "[generate]") {
  const auto cases = cms::generate::build_cases(specification(), CONSTANT_GENERATOR, SUM_REFERENCE, 1000);
  REQUIRE(cases.has_value());
  auto answer = answer_with();
  answer.cases = *cases;

  const auto scores =
      cms::generate::score_solutions(answer, SUM_REFERENCE, {{1, std::string{SUM_REFERENCE}}}, 1000);

  REQUIRE(!scores.has_value());
  CHECK(scores.error().message.find("accepted on every case") != std::string::npos);
  CHECK(scores.error().message.find("not a partial") != std::string::npos);
}

TEST_CASE("a subtask whose narrowing leaves the task's bounds is refused by name", "[generate]") {
  auto spec = specification();
  spec.subtasks = {cms::generate::SubtaskConstraint{.position = 1,
                                                     .bounds = cms::SubtaskBounds{.max_n = 300000}}};
  const auto cases = cms::generate::build_cases(spec, CONSTANT_GENERATOR, SUM_REFERENCE, 1000);

  REQUIRE(!cases.has_value());
  CHECK(cases.error().message.find("subtask 1") != std::string::npos);
  CHECK(cases.error().message.find("300000") != std::string::npos);
}

TEST_CASE("two calls: one for the generator, one for the checker", "[generate]") {
  auto client = cms::generate::RecordedClient{"tests", "tests-v1", 9, {std::string{CONSTANT_GENERATOR},
                                                                      std::string{TOKEN_CHECKER}}};

  const auto scores =
      cms::generate::write_test_plan(client, specification(), SUM_REFERENCE, SUM_REFERENCE, partials(), 1000);

  if (!scores) {
    FAIL("write_test_plan refused: " + scores.error().message);
  }
  REQUIRE(scores.has_value());
  CHECK(client.prompts().size() == 2);
  CHECK(client.prompts()[0].find("seed and n") != std::string::npos);
  CHECK(client.prompts()[1].find("exits 0 when") != std::string::npos);
}

TEST_CASE("step 6 records two program rows and one case row per subtask", "[generate][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();
  const auto job = cms::generate::test::prepare_job(database, "pipeline-tests-probe");
  auto store = cms::generate::test::MemoryStore{};
  auto sink = cms::generate::SqlTestPlanSink{*cms::generate::test::live_client(), store};
  const auto cases = cms::generate::build_cases(specification(), CONSTANT_GENERATOR, SUM_REFERENCE, 1000);
  REQUIRE(cases.has_value());
  auto answer = answer_with();
  answer.cases = *cases;
  const auto scores = cms::generate::score_solutions(answer, SUM_REFERENCE, {}, 1000);
  REQUIRE(!scores.has_value());

  const auto written = sink.record(job, answer, {});

  REQUIRE(written.has_value());
  CHECK(*written == 4);

  auto programs = 0;
  for (const auto& row : database(sqlpp::select(cms::db::generationPrograms.role)
                                      .from(cms::db::generationPrograms)
                                      .where(cms::db::generationPrograms.jobId == job))) {
    const auto role = std::string{row.role};
    CHECK((role == "generator" || role == "checker"));
    ++programs;
  }
  CHECK(programs == 2);

  auto case_rows = 0;
  for (const auto& row : database(sqlpp::select(cms::db::generationCases.caseCount)
                                      .from(cms::db::generationCases)
                                      .where(cms::db::generationCases.jobId == job))) {
    CHECK(row.caseCount > 0);
    ++case_rows;
  }
  CHECK(case_rows == 2);
}
