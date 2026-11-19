// Step 5 writes the reference and one solution per subtask, and proves each earned exactly its group: the reference
// against the step-3 oracle on the smallest cases, a partial by passing its own bound and failing one case beyond it.

#include <cms/generate/model.hpp>
#include <cms/generate/solution.hpp>
#include <cms/generate/specification.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
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

/// A program that refuses to answer: ADR 0042 makes that a refusal, never an answer that differs.
constexpr std::string_view EXIT_PROGRAM = R"(int main() { return 1; }
)";

constexpr std::string_view SUM_PROGRAM = R"(#include <iostream>
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

/// A partial that sums only the first hundred thousand values: right up to n = 100000, wrong after it.
constexpr std::string_view HUNDRED_THOUSAND_PROGRAM = R"(#include <iostream>
int main() {
  long long n = 0;
  std::cin >> n;
  long long total = 0;
  for (long long index = 0; index < n; ++index) {
    long long value = 0;
    std::cin >> value;
    if (index < 100000) {
      total += value;
    }
  }
  std::cout << total << "\n";
  return 0;
}
)";

constexpr std::string_view WRONG_PROGRAM = R"(#include <iostream>
int main() {
  std::cout << 999 << "\n";
  return 0;
}
)";

/// A partial that only looks at the first hundred values: right up to n = 100, wrong after it.
constexpr std::string_view HUNDRED_PROGRAM = R"(#include <iostream>
int main() {
  long long n = 0;
  std::cin >> n;
  long long total = 0;
  for (long long index = 0; index < n; ++index) {
    long long value = 0;
    std::cin >> value;
    if (index < 100) {
      total += value;
    }
  }
  std::cout << total << "\n";
  return 0;
}
)";

auto specification(std::vector<SubtaskConstraint> subtasks = {
                       SubtaskConstraint{.position = 1, .bounds = cms::SubtaskBounds{.max_n = 100}},
                       SubtaskConstraint{.position = 2, .bounds = cms::SubtaskBounds{.max_n = 100000}}})
    -> Specification {
  return Specification{.input = "n, then n integers",
                       .output = "their sum",
                       .bounds = {.min_n = 1, .max_n = 200000, .value_min = 3, .value_max = 3},
                       .subtasks = std::move(subtasks)};
}

auto call(std::int32_t index = 0) -> ModelCall {
  return ModelCall{.model_id = "solution", .prompt_version = "sol-v1", .seed = 5, .call_index = index};
}

auto answer_with(std::vector<std::string> partials, std::string_view reference = SUM_PROGRAM)
    -> cms::generate::Step5Answer {
  auto answer = cms::generate::Step5Answer{.reference = {.position = 0, .source = std::string{reference}, .call = call(0)},
                                           .partials = {},
                                           .reference_answer = std::string{SUM_PROGRAM}};
  for (std::size_t index = 0; index < partials.size(); ++index) {
    answer.partials.push_back(cms::generate::WrittenSolution{
        .position = static_cast<std::int32_t>(index + 1), .source = partials[index],
        .call = call(static_cast<std::int32_t>(index + 1))});
  }
  return answer;
}

}  // namespace

TEST_CASE("cases come from the typed bounds: inside the subtask, and one step beyond it", "[generate]") {
  const auto cases = cms::generate::subtask_cases(
      specification(), SubtaskConstraint{.position = 1, .bounds = cms::SubtaskBounds{.max_n = 100}});

  REQUIRE(cases.has_value());
  REQUIRE(cases->inside.size() == 2);
  CHECK(cases->inside[0].input == "1\n3\n");
  CHECK(cases->inside[1].n == 100);
  CHECK(std::count(cases->inside[1].input.begin(), cases->inside[1].input.end(), '3') == 100);
  REQUIRE(cases->outside.size() == 1);
  CHECK(cases->outside[0].n == 101);
}

TEST_CASE("a subtask that admits every case the task admits names no case outside it, and is refused", "[generate]") {
  const auto cases = cms::generate::subtask_cases(
      specification(), SubtaskConstraint{.position = 1, .bounds = cms::SubtaskBounds{
                                                          .min_n = 1, .max_n = 200000, .value_min = 3,
                                                          .value_max = 3}});

  REQUIRE(!cases.has_value());
  CHECK(cases.error().message.find("admits every case the task admits") != std::string::npos);
}

TEST_CASE("the reference is refused when it disagrees with the oracle, and the sentence names the case", "[generate]") {
  const auto cases = cms::generate::admissible_cases(
      specification(), SubtaskConstraint{.position = 1, .bounds = cms::SubtaskBounds{.max_n = 100}});
  REQUIRE(cases.has_value());
  const auto answer = answer_with({std::string{SUM_PROGRAM}, std::string{SUM_PROGRAM}}, WRONG_PROGRAM);

  const auto proofs = cms::generate::check_solutions(answer, specification(), SUM_PROGRAM, *cases, 1000);

  REQUIRE(!proofs.has_value());
  CHECK(proofs.error().message.find("disagrees with the oracle") != std::string::npos);
  CHECK(proofs.error().message.find("at n = 1") != std::string::npos);
  CHECK(proofs.error().message.find("999") != std::string::npos);
}

TEST_CASE("a reference that exits non-zero is a refusal, not a different answer", "[generate]") {
  const auto cases = cms::generate::admissible_cases(
      specification(), SubtaskConstraint{.position = 1, .bounds = cms::SubtaskBounds{.max_n = 100}});
  REQUIRE(cases.has_value());
  const auto answer = answer_with({std::string{SUM_PROGRAM}, std::string{SUM_PROGRAM}}, EXIT_PROGRAM);

  const auto proofs = cms::generate::check_solutions(answer, specification(), SUM_PROGRAM, *cases, 1000);

  REQUIRE(!proofs.has_value());
  CHECK(proofs.error().message.find("the reference solution") != std::string::npos);
  CHECK(proofs.error().message.find("exit 1") != std::string::npos);
  CHECK(proofs.error().message.find("disagrees") == std::string::npos);
}

TEST_CASE("a partial that passes everything is refused, because it is not a partial", "[generate]") {
  const auto cases = cms::generate::admissible_cases(
      specification(), SubtaskConstraint{.position = 1, .bounds = cms::SubtaskBounds{.max_n = 100}});
  REQUIRE(cases.has_value());
  const auto answer = answer_with({std::string{SUM_PROGRAM}, std::string{SUM_PROGRAM}});

  const auto proofs = cms::generate::check_solutions(answer, specification(), SUM_PROGRAM, *cases, 1000);

  REQUIRE(!proofs.has_value());
  CHECK(proofs.error().message.find("subtask 1") != std::string::npos);
  CHECK(proofs.error().message.find("passes the case outside its subtask") != std::string::npos);
}

TEST_CASE("a partial that loses the case beyond its bound earns its proof", "[generate]") {
  const auto cases = cms::generate::admissible_cases(
      specification(), SubtaskConstraint{.position = 1, .bounds = cms::SubtaskBounds{.max_n = 100}});
  REQUIRE(cases.has_value());
  const auto answer = answer_with({std::string{HUNDRED_PROGRAM}, std::string{HUNDRED_THOUSAND_PROGRAM}});

  const auto proofs = cms::generate::check_solutions(answer, specification(), SUM_PROGRAM, *cases, 1000);

  REQUIRE(proofs.has_value());
  REQUIRE(proofs->size() == 2);
  CHECK(proofs->front().inside_cases == 2);
  CHECK(proofs->front().failed_outside);
  CHECK(proofs->front().sentence.find("subtask 1") != std::string::npos);
  CHECK(proofs->front().sentence.find("passed 2 case(s)") != std::string::npos);
}

TEST_CASE("a subtask with no solution is refused by name, with its position", "[generate]") {
  const auto cases = cms::generate::admissible_cases(
      specification(), SubtaskConstraint{.position = 1, .bounds = cms::SubtaskBounds{.max_n = 100}});
  REQUIRE(cases.has_value());
  const auto answer = answer_with({std::string{HUNDRED_PROGRAM}});

  const auto proofs = cms::generate::check_solutions(answer, specification(), SUM_PROGRAM, *cases, 1000);

  REQUIRE(!proofs.has_value());
  CHECK(proofs.error().message.find("subtask 2 has no solution") != std::string::npos);
}

TEST_CASE("a solution that does not compile is refused by name, with its position", "[generate]") {
  const auto cases = cms::generate::admissible_cases(
      specification(), SubtaskConstraint{.position = 1, .bounds = cms::SubtaskBounds{.max_n = 100}});
  REQUIRE(cases.has_value());
  const auto answer = answer_with({std::string{"int main() { this is not C++ }"}, std::string{HUNDRED_PROGRAM}});

  const auto proofs = cms::generate::check_solutions(answer, specification(), SUM_PROGRAM, *cases, 1000);

  REQUIRE(!proofs.has_value());
  CHECK(proofs.error().message.find("subtask 1") != std::string::npos);
  CHECK(proofs.error().message.find("does not compile") != std::string::npos);
}

TEST_CASE("one call per program: the reference and each subtask", "[generate]") {
  auto client = cms::generate::RecordedClient{"solution", "sol-v1", 5,
                                              {std::string{SUM_PROGRAM}, std::string{HUNDRED_PROGRAM},
                                               std::string{HUNDRED_PROGRAM}}};

  const auto answer = cms::generate::write_solutions(client, specification(), SUM_PROGRAM);

  REQUIRE(answer.has_value());
  CHECK(answer->reference.position == 0);
  CHECK(answer->partials.size() == 2);
  CHECK(answer->partials[0].call.call_index == 1);
  CHECK(client.prompts().size() == 3);
  CHECK(client.prompts()[1].find("subtask 1") != std::string::npos);
  CHECK(client.prompts()[1].find("must fail at least one case") != std::string::npos);
}

TEST_CASE("a specification with no subtask has no partial to write, and is refused", "[generate]") {
  auto client = cms::generate::RecordedClient{"solution", "sol-v1", 5, {std::string{SUM_PROGRAM}}};

  const auto answer = cms::generate::write_solutions(client, specification({}), SUM_PROGRAM);

  REQUIRE(!answer.has_value());
  CHECK(answer.error().message.find("names no subtask") != std::string::npos);
}

TEST_CASE("step 5 records one row per program in one transaction",
          "[generate][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();
  const auto job = cms::generate::test::prepare_job(database, "pipeline-solution-probe");
  auto store = cms::generate::test::MemoryStore{};
  auto sink = cms::generate::SqlSolutionSink{*cms::generate::test::live_client(), store};
  const auto cases = cms::generate::admissible_cases(
      specification(), SubtaskConstraint{.position = 1, .bounds = cms::SubtaskBounds{.max_n = 100}});
  REQUIRE(cases.has_value());
  const auto answer = answer_with({std::string{HUNDRED_PROGRAM}, std::string{HUNDRED_THOUSAND_PROGRAM}});
  const auto proofs = cms::generate::check_solutions(answer, specification(), SUM_PROGRAM, *cases, 1000);
  REQUIRE(proofs.has_value());

  const auto written = sink.record(job, answer, *proofs);

  REQUIRE(written.has_value());
  CHECK(*written == 3);

  auto artefacts = 0;
  for (const auto& row : database(sqlpp::select(cms::db::generationArtefacts.kind)
                                      .from(cms::db::generationArtefacts)
                                      .where(cms::db::generationArtefacts.jobId == job))) {
    const auto kind = std::string{row.kind};
    CHECK((kind == "reference" || kind == "subtask-solution"));
    ++artefacts;
  }
  CHECK(artefacts == 3);

  auto rows = 0;
  for (const auto& row : database(sqlpp::select(cms::db::generationSolutions.position,
                                               cms::db::generationSolutions.proof)
                                      .from(cms::db::generationSolutions)
                                      .where(cms::db::generationSolutions.jobId == job))) {
    const auto proof = std::string{row.proof};
    CHECK((proof.find("oracle") != std::string::npos || proof.find("subtask") != std::string::npos));
    ++rows;
  }
  CHECK(rows == 3);
}
