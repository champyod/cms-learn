// Step 3 turns the chosen plan into the three facts a later claim is checked against: a specification, one limit
// pair derived from the plan's complexity at the plan's bound, and a brute-force oracle that is stored, read back
// and run on the smallest case the bounds admit.

#include <cms/generate/candidate.hpp>
#include <cms/generate/model.hpp>
#include <cms/generate/specification.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <sqlpp23/sqlpp23.h>

#include <cms/db/connection.hpp>
#include <cms/db/tables.hpp>
#include <cms/generate/artefact.hpp>
#include <cms/object_store.hpp>

#include "live_database.hpp"
#include "memory_store.hpp"
#include "pipeline_fixture.hpp"

namespace {

using cms::generate::Bounds;
using cms::generate::ModelCall;
using cms::generate::Specification;

constexpr std::string_view SUM_ORACLE = R"(#include <iostream>
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

/// @return @p text as the body of a JSON string, so a program's newlines do not break the answer.
auto json_string(std::string_view text) -> std::string {
  auto out = std::string{"\""};
  for (const auto character : text) {
    if (character == '\n') {
      out += "\\n";
      continue;
    }
    if (character == '"' || character == '\\') {
      out.push_back('\\');
    }
    out.push_back(character);
  }
  out.push_back('"');
  return out;
}

auto answer_json(std::string_view input_shape = "n_then_values", std::int64_t min_n = 1,
                 std::int64_t max_n = 200000, std::string_view oracle = SUM_ORACLE) -> std::string {
  return "{\"input\": \"n, then n integers\", \"output\": \"their sum\", \"input_shape\": \"" +
         std::string{input_shape} + "\", \"bounds\": {\"min_n\": " + std::to_string(min_n) +
         ", \"max_n\": " + std::to_string(max_n) + ", \"value_min\": -1000000000, \"value_max\": 1000000000}, "
         "\"subtasks\": [{\"position\": 1, \"constraint\": \"n <= 100\"}], \"oracle\": " +
         json_string(oracle) + "}";
}

auto chosen(std::string_view complexity = "O(n log n)") -> cms::generate::CandidatePlan {
  return cms::generate::CandidatePlan{.approach = "prefix sums",
                                      .intended_complexity = std::string{complexity},
                                      .fits_bounds = true,
                                      .plan_json = "{\"approach\": \"prefix sums\", \"n_max\": 200000}",
                                      .call = ModelCall{.model_id = "planner", .prompt_version = "v1", .seed = 7,
                                                        .call_index = 0}};
}

auto call() -> ModelCall {
  return ModelCall{.model_id = "proof", .prompt_version = "spec-v1", .seed = 11, .call_index = 0};
}

}  // namespace

TEST_CASE("a limit is derived from the complexity at the bound, and says so", "[generate]") {
  const auto limit = cms::generate::derive_limit(
      "O(n log n)", Bounds{.min_n = 1, .max_n = 200000, .value_min = -1000, .value_max = 1000});

  REQUIRE(limit.has_value());
  CHECK(limit->time_limit_ms == 100);
  CHECK(limit->memory_limit_mb == 16);
  CHECK(limit->derivation.find("O(n log n) at n = 200000") != std::string::npos);
  CHECK(limit->derivation.find("operations per millisecond") != std::string::npos);
  CHECK(limit->derivation.find("inside 100..20000 ms") != std::string::npos);
}

TEST_CASE("a derivation outside the band is refused by name, never clamped", "[generate]") {
  const auto refused = cms::generate::derive_limit(
      "O(2^n)", Bounds{.min_n = 1, .max_n = 40, .value_min = 0, .value_max = 0});

  REQUIRE(!refused.has_value());
  CHECK(refused.error().message.find("outside the allowed band") != std::string::npos);
  CHECK(refused.error().message.find("O(2^n) at n = 40") != std::string::npos);
  CHECK(refused.error().message.find("100..20000 ms") != std::string::npos);
}

TEST_CASE("a plan with no bound, or a complexity this step cannot read, is refused by name", "[generate]") {
  const auto no_bound = cms::generate::derive_limit(
      "O(n)", Bounds{.min_n = 0, .max_n = 0, .value_min = 0, .value_max = 0});
  REQUIRE(!no_bound.has_value());
  CHECK(no_bound.error().message.find("names no bound") != std::string::npos);

  const auto unknown = cms::generate::operations_at("O(n log log n)", 1000);
  REQUIRE(!unknown.has_value());
  CHECK(unknown.error().message.find("O(n log log n)") != std::string::npos);
  CHECK(unknown.error().message.find("reads O(1), O(log n), O(n), O(n log n)") != std::string::npos);
}

TEST_CASE("the smallest case comes from the bounds, and a shape this step cannot sample is refused", "[generate]") {
  auto specification = Specification{};
  specification.bounds = Bounds{.min_n = 3, .max_n = 200000, .value_min = -7, .value_max = 9};
  const auto small = cms::generate::sample_smallest(specification);
  REQUIRE(small.has_value());
  CHECK(*small == "3\n-7 -7 -7\n");

  specification.bounds.min_n = 0;
  const auto refused = cms::generate::sample_smallest(specification);
  REQUIRE(!refused.has_value());
  CHECK(refused.error().message.find("no smallest n") != std::string::npos);
}

TEST_CASE("one model call produces the specification, the derived limit and the oracle", "[generate]") {
  auto client = cms::generate::RecordedClient{"proof", "spec-v1", 11, {answer_json()}};

  const auto answer = cms::generate::specify_step3(client, chosen());

  REQUIRE(answer.has_value());
  CHECK(answer->specification.input == "n, then n integers");
  CHECK(answer->specification.subtasks.size() == 1);
  CHECK(answer->specification.subtasks[0].constraint == "n <= 100");
  CHECK(answer->limit.time_limit_ms == 100);
  CHECK(answer->oracle_source.find("int main") != std::string::npos);
  CHECK(answer->call.prompt_version == "spec-v1");
  CHECK(client.prompts().size() == 1);
  CHECK(client.prompts()[0].find("the chosen plan") != std::string::npos);
}

TEST_CASE("an answer with no oracle, or an input shape this step cannot sample, is refused by name", "[generate]") {
  auto no_oracle = cms::generate::parse_specification_answer(
      "{\"input\": \"n\", \"output\": \"n\", \"input_shape\": \"n_then_values\", "
      "\"bounds\": {\"min_n\": 1, \"max_n\": 10, \"value_min\": 0, \"value_max\": 1}, \"subtasks\": []}",
      call());
  REQUIRE(!no_oracle.has_value());
  CHECK(no_oracle.error().message.find("no oracle program") != std::string::npos);

  auto unknown_shape = cms::generate::parse_specification_answer(answer_json("a_tree"), call());
  REQUIRE(!unknown_shape.has_value());
  CHECK(unknown_shape.error().message.find("a_tree") != std::string::npos);
  CHECK(unknown_shape.error().message.find("n_then_values") != std::string::npos);
}

TEST_CASE("the oracle is a real program: it compiles and answers the smallest case", "[generate]") {
  const auto checked = cms::generate::check_oracle(SUM_ORACLE, "3\n1 2 3\n", 1000);

  REQUIRE(checked.has_value());
  CHECK(checked->answer == "6\n");
}

TEST_CASE("an oracle that does not build, or answers nothing, is refused by name", "[generate]") {
  const auto broken = cms::generate::check_oracle("int main() { this is not C++ }", "1\n0\n", 1000);
  REQUIRE(!broken.has_value());
  CHECK(broken.error().message.find("does not compile") != std::string::npos);

  const auto silent = cms::generate::check_oracle("int main() { return 0; }", "1\n0\n", 1000);
  REQUIRE(!silent.has_value());
  CHECK(silent.error().message.find("answered the smallest case with nothing") != std::string::npos);
}

TEST_CASE("step 3's record is one specification row and two call rows in one transaction", "[generate][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();
  const auto job = cms::generate::test::prepare_job(database, "pipeline-proof-probe");
  auto store = cms::generate::test::MemoryStore{};
  auto client = cms::generate::RecordedClient{"proof", "spec-v1", 11, {answer_json()}};
  auto sink = cms::generate::SqlSpecificationSink{*cms::generate::test::live_client(), store};

  const auto answer = cms::generate::specify_step3(client, chosen());
  REQUIRE(answer.has_value());
  const auto smallest = cms::generate::sample_smallest(answer->specification);
  REQUIRE(smallest.has_value());
  const auto checked = cms::generate::check_oracle(answer->oracle_source, *smallest, answer->limit.time_limit_ms);
  REQUIRE(checked.has_value());

  const auto digest = sink.record(job, *answer, *checked);
  REQUIRE(digest.has_value());

  auto rows = 0;
  for (const auto& row : database(sqlpp::select(cms::db::generationArtefacts.id)
                                      .from(cms::db::generationArtefacts)
                                      .where(cms::db::generationArtefacts.jobId == job))) {
    (void)row;
    ++rows;
  }
  CHECK(rows == 2);

  const auto stored = cms::generate::read_artefact(*cms::generate::test::live_client(), store, job, 3, "oracle");
  REQUIRE(stored.has_value());
  CHECK(stored->digest == *digest);
  CHECK(stored->bytes == answer->oracle_source);

  auto recorded = std::vector<std::string>{};
  for (const auto& row : database(sqlpp::select(cms::db::generationSpecifications.timeLimitMs,
                                               cms::db::generationSpecifications.memoryLimitMb,
                                               cms::db::generationSpecifications.derivation)
                                      .from(cms::db::generationSpecifications)
                                      .where(cms::db::generationSpecifications.jobId == job))) {
    CHECK(row.timeLimitMs == 100);
    CHECK(row.memoryLimitMb == 16);
    recorded.emplace_back(row.derivation);
  }
  REQUIRE(recorded.size() == 1);
  CHECK(recorded[0].find("O(n log n) at n = 200000") != std::string::npos);
}
