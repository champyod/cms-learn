// Step 1 asks for four independent candidate plans and refuses anything that is not four distinct ones.

#include <cms/generate/brief.hpp>
#include <cms/generate/model.hpp>
#include <cms/generate/planner.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr auto MODEL_ID = "deployer-model";
constexpr auto PROMPT_VERSION = "planner-v1";
constexpr std::int64_t SEED = 20261029;

auto brief() -> cms::generate::Brief {
  auto value = cms::generate::Brief{};
  value.technique = "prefix-sums";
  value.difficulty = "1800";
  value.topics = {"prefix-sums", "range-query"};
  value.locales = {"en", "th"};
  value.io = "n and q, then the sequence, then q pairs";
  value.brief = "one query over a sequence";
  return value;
}

auto answer(std::string_view approach, bool fits = true) -> std::string {
  return "{\"approach\": \"" + std::string{approach} +
         "\", \"intended_complexity\": \"O(n log n)\", \"fits_bounds\": " + (fits ? "true" : "false") +
         ", \"constraints\": {\"n\": 200000}}";
}

auto four_answers() -> std::vector<std::string> {
  return {answer("prefix sums"), answer("segment tree"), answer("sqrt decomposition"),
          answer("mo's algorithm")};
}

auto planner_for(std::vector<std::string> answers) -> cms::generate::RecordedClient {
  return cms::generate::RecordedClient{MODEL_ID, PROMPT_VERSION, SEED, std::move(answers)};
}

}  // namespace

TEST_CASE("four candidates come from four independent calls", "[generate]") {
  auto client = planner_for(four_answers());

  const auto planned = cms::generate::plan_candidates(client, brief());

  REQUIRE(planned.has_value());
  CHECK(planned->size() == cms::generate::MINIMUM_CANDIDATES);
  CHECK(client.prompts().size() == cms::generate::MINIMUM_CANDIDATES);
  CHECK((*planned)[0].approach == "prefix sums");
  CHECK((*planned)[3].approach == "mo's algorithm");
}

TEST_CASE("each candidate carries the call that produced it", "[generate]") {
  auto client = planner_for(four_answers());

  const auto planned = cms::generate::plan_candidates(client, brief());

  REQUIRE(planned.has_value());
  for (std::size_t index = 0; index < planned->size(); ++index) {
    CHECK((*planned)[index].call.model_id == MODEL_ID);
    CHECK((*planned)[index].call.prompt_version == PROMPT_VERSION);
    CHECK((*planned)[index].call.seed == SEED);
    CHECK((*planned)[index].call.call_index == static_cast<std::int32_t>(index));
  }
}

TEST_CASE("the plan body is kept as one minified JSON object", "[generate]") {
  auto answers = four_answers();
  answers[0] = "{\n  \"approach\": \"prefix sums\",\n  \"intended_complexity\": \"O(n + q)\",\n"
               "  \"fits_bounds\": true\n}\n";
  auto client = planner_for(answers);

  const auto planned = cms::generate::plan_candidates(client, brief());

  REQUIRE(planned.has_value());
  CHECK((*planned)[0].plan_json.find('\n') == std::string::npos);
  CHECK((*planned)[0].plan_json.find("\"approach\":\"prefix sums\"") != std::string::npos);
  CHECK((*planned)[0].intended_complexity == "O(n + q)");
}

TEST_CASE("each shard is told which candidate it is and what the teacher set", "[generate]") {
  const auto prompt = cms::generate::planner_prompt(brief(), 2, 4);

  CHECK(prompt.find("candidate 2 of 4") != std::string::npos);
  CHECK(prompt.find("technique: prefix-sums") != std::string::npos);
  CHECK(prompt.find("difficulty: 1800") != std::string::npos);
  CHECK(prompt.find("fits_bounds") != std::string::npos);
}

TEST_CASE("a run below the floor of four is refused before a call", "[generate]") {
  auto client = planner_for(four_answers());

  const auto planned = cms::generate::plan_candidates(client, brief(), 3);

  REQUIRE(!planned.has_value());
  CHECK(planned.error().message.find("at least 4") != std::string::npos);
  CHECK(client.prompts().empty());
}

TEST_CASE("a brief without the rows the form requires is refused before a call", "[generate]") {
  auto client = planner_for(four_answers());
  auto incomplete = brief();
  incomplete.difficulty = "  ";

  const auto planned = cms::generate::plan_candidates(client, incomplete);

  REQUIRE(!planned.has_value());
  CHECK(planned.error().message.find("difficulty") != std::string::npos);
  CHECK(planned.error().message.find("technique") == std::string::npos);
  CHECK(client.prompts().empty());
}

TEST_CASE("four candidates that share one approach are refused as one plan written four times",
          "[generate]") {
  auto answers = four_answers();
  answers[2] = answer("Prefix   Sums");
  auto client = planner_for(answers);

  const auto planned = cms::generate::plan_candidates(client, brief());

  REQUIRE(!planned.has_value());
  CHECK(planned.error().message.find("same approach") != std::string::npos);
}

TEST_CASE("an answer that is not a candidate names the call it arrived in", "[generate]") {
  auto answers = four_answers();
  answers[1] = "{\"approach\": \"\", \"fits_bounds\": true}";
  auto client = planner_for(answers);

  const auto planned = cms::generate::plan_candidates(client, brief());

  REQUIRE(!planned.has_value());
  CHECK(planned.error().message.find("call 2") != std::string::npos);
  CHECK(planned.error().message.find("intended_complexity") != std::string::npos);
}

TEST_CASE("a shard that answers with prose is refused naming the call", "[generate]") {
  auto answers = four_answers();
  answers[0] = "Here is a plan: use a segment tree.";
  auto client = planner_for(answers);

  const auto planned = cms::generate::plan_candidates(client, brief());

  REQUIRE(!planned.has_value());
  CHECK(planned.error().message.find("call 1") != std::string::npos);
  CHECK(planned.error().message.find("not valid JSON") != std::string::npos);
}

TEST_CASE("a recording that runs out ends the step on the call that was not recorded", "[generate]") {
  auto client = planner_for({answer("prefix sums"), answer("segment tree"), answer("sqrt decomposition")});

  const auto planned = cms::generate::plan_candidates(client, brief());

  REQUIRE(!planned.has_value());
  CHECK(planned.error().message.find("recording holds 3") != std::string::npos);
}
