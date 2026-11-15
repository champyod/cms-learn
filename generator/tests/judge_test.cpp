// Step 2 chooses one plan and keeps every other one beside the reason it lost; a decision that leaves a
// candidate unexplained, invents a position, or chooses a plan that cannot fit the bounds is refused.

#include <cms/generate/candidate.hpp>
#include <cms/generate/judge.hpp>
#include <cms/generate/model.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

auto candidate(std::string_view approach, bool fits = true) -> cms::generate::CandidatePlan {
  return cms::generate::CandidatePlan{
      .approach = std::string{approach},
      .intended_complexity = "O(n log n)",
      .fits_bounds = fits,
      .plan_json = "{\"approach\":\"" + std::string{approach} + "\"}",
      .call = cms::generate::ModelCall{
          .model_id = "deployer-model", .prompt_version = "planner-v1", .seed = 7, .call_index = 0}};
}

auto four_candidates() -> std::vector<cms::generate::CandidatePlan> {
  return {candidate("prefix sums", false), candidate("segment tree"), candidate("sqrt decomposition"),
          candidate("mo's algorithm")};
}

auto answer(std::int64_t chosen, std::string_view reason,
            const std::vector<std::pair<std::int64_t, std::string>>& rejected) -> std::string {
  auto entries = std::string{};
  for (const auto& [position, why] : rejected) {
    if (!entries.empty()) {
      entries += ", ";
    }
    entries += "{\"position\": " + std::to_string(position) + ", \"reason\": \"" + why + "\"}";
  }
  return "{\"chosen\": " + std::to_string(chosen) + ", \"reason\": \"" + std::string{reason} +
         "\", \"rejected\": [" + entries + "]}";
}

auto three_rejections() -> std::vector<std::pair<std::int64_t, std::string>> {
  return {{1, "its cost does not fit the bounds"}, {3, "more machinery than the task needs"},
          {4, "a worse constant, no simpler proof"}};
}

auto judge_for(std::string decision) -> cms::generate::RecordedClient {
  return cms::generate::RecordedClient{"judge-model", "judge-v1", 11, {std::move(decision)}};
}

}  // namespace

TEST_CASE("the chosen plan and the reason every other one lost are both recorded", "[generate]") {
  auto client = judge_for(answer(2, "cheapest plan that fits", three_rejections()));

  const auto judged = cms::generate::judge_candidates(client, four_candidates());

  REQUIRE(judged.has_value());
  REQUIRE(judged->records.size() == 4);
  CHECK(cms::generate::chosen_of(*judged).position == 2);
  CHECK(judged->records[1].chosen);
  CHECK(judged->records[1].reason == "cheapest plan that fits");
  CHECK(judged->records[0].reason == "its cost does not fit the bounds");
  CHECK(judged->records[2].reason == "more machinery than the task needs");
  CHECK(judged->records[3].reason == "a worse constant, no simpler proof");
  CHECK(judged->call.model_id == "judge-model");
}

TEST_CASE("every record keeps the position and the plan it was handed", "[generate]") {
  auto client = judge_for(answer(2, "fits", three_rejections()));

  const auto judged = cms::generate::judge_candidates(client, four_candidates());

  REQUIRE(judged.has_value());
  for (std::size_t index = 0; index < judged->records.size(); ++index) {
    CHECK(judged->records[index].position == static_cast<std::int32_t>(index + 1));
  }
  CHECK(judged->records[2].plan.approach == "sqrt decomposition");
  CHECK(judged->records[0].plan.plan_json == "{\"approach\":\"prefix sums\"}");
}

TEST_CASE("exactly one plan is chosen", "[generate]") {
  auto client = judge_for(answer(3, "fits", {{1, "no"}, {2, "no"}, {4, "no"}}));

  const auto judged = cms::generate::judge_candidates(client, four_candidates());

  REQUIRE(judged.has_value());
  auto chosen = 0;
  for (const auto& record : judged->records) {
    chosen += record.chosen ? 1 : 0;
  }
  CHECK(chosen == 1);
}

TEST_CASE("a candidate the judge leaves unexplained is refused by position", "[generate]") {
  auto client = judge_for(answer(2, "fits", {{1, "no"}, {3, "no"}}));

  const auto judged = cms::generate::judge_candidates(client, four_candidates());

  REQUIRE(!judged.has_value());
  CHECK(judged.error().message.find("no reason for position 4") != std::string::npos);
}

TEST_CASE("a reason written into the chosen position is not a reason against it", "[generate]") {
  auto client = judge_for(answer(2, "fits", {{1, "no"}, {2, "no"}, {3, "no"}, {4, "no"}}));

  const auto judged = cms::generate::judge_candidates(client, four_candidates());

  REQUIRE(!judged.has_value());
  CHECK(judged.error().message.find("rejected position") != std::string::npos);
}

TEST_CASE("a position no candidate holds is refused", "[generate]") {
  auto client = judge_for(answer(2, "fits", {{1, "no"}, {3, "no"}, {9, "no"}}));

  const auto judged = cms::generate::judge_candidates(client, four_candidates());

  REQUIRE(!judged.has_value());
  CHECK(judged.error().message.find("rejected position") != std::string::npos);
}

TEST_CASE("a chosen position outside the candidates is refused", "[generate]") {
  auto client = judge_for(answer(5, "fits", three_rejections()));

  const auto judged = cms::generate::judge_candidates(client, four_candidates());

  REQUIRE(!judged.has_value());
  CHECK(judged.error().message.find("between 1 and 4") != std::string::npos);
}

TEST_CASE("a blank reason for the choice is refused", "[generate]") {
  auto client = judge_for(answer(2, "   ", three_rejections()));

  const auto judged = cms::generate::judge_candidates(client, four_candidates());

  REQUIRE(!judged.has_value());
  CHECK(judged.error().message.find("reason") != std::string::npos);
}

TEST_CASE("choosing a plan that cannot fit the declared bounds is refused, not widened", "[generate]") {
  auto client = judge_for(answer(1, "it looks clever", {{2, "no"}, {3, "no"}, {4, "no"}}));

  const auto judged = cms::generate::judge_candidates(client, four_candidates());

  REQUIRE(!judged.has_value());
  CHECK(judged.error().message.find("does not fit the declared bounds") != std::string::npos);
}

TEST_CASE("step 2 refuses a set it cannot choose between", "[generate]") {
  auto client = judge_for(answer(1, "fits", {}));

  const auto judged = cms::generate::judge_candidates(client, {candidate("prefix sums")});

  REQUIRE(!judged.has_value());
  CHECK(judged.error().message.find("was handed 1") != std::string::npos);
}

TEST_CASE("the judge is shown the plan it is choosing between, numbered from one", "[generate]") {
  const auto prompt = cms::generate::judge_prompt(four_candidates());

  REQUIRE(prompt.has_value());
  CHECK(prompt->find("candidate 1") != std::string::npos);
  CHECK(prompt->find("candidate 4") != std::string::npos);
  CHECK(prompt->find("approach: mo's algorithm") != std::string::npos);
  CHECK(prompt->find("fits the declared bounds: no") != std::string::npos);
}
