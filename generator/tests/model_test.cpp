// The model interface and its recorded stub: every step's test runs through these, with no network and no
// credential, and every artefact must carry the call it came from (model id, prompt version, seed, call index).

#include <cms/generate/config.hpp>
#include <cms/generate/model.hpp>

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <vector>

namespace {

auto lookup_from(std::map<std::string, std::string> values)
    -> std::function<std::string(std::string_view)> {
  return [values = std::move(values)](std::string_view name) {
    const auto found = values.find(std::string{name});
    return found == values.end() ? std::string{} : found->second;
  };
}

}  // namespace

TEST_CASE("the recorded client answers in order and records each call", "[generate]") {
  auto client = cms::generate::RecordedClient{"deployer-model", "prompt-v3", 4242,
                                              {"first answer", "second answer"}};

  const auto first = client.complete("plan please");
  REQUIRE(first.has_value());
  CHECK(first->text == "first answer");
  CHECK(first->call.model_id == "deployer-model");
  CHECK(first->call.prompt_version == "prompt-v3");
  CHECK(first->call.seed == 4242);
  CHECK(first->call.call_index == 0);

  const auto second = client.complete("judge please");
  REQUIRE(second.has_value());
  CHECK(second->call.call_index == 1);
  CHECK(client.prompts().size() == 2);
  CHECK(client.prompts().front() == "plan please");
}

TEST_CASE("a recording that runs out is a refusal, not an empty completion", "[generate]") {
  auto client = cms::generate::RecordedClient{"m", "v", 1, {"only"}};
  REQUIRE(client.complete("one").has_value());
  const auto exhausted = client.complete("two");
  REQUIRE(!exhausted.has_value());
  CHECK(exhausted.error().message.find("recording holds 1") != std::string::npos);
}

TEST_CASE("the worker's configuration names every missing key and no value", "[generate]") {
  const auto nothing = cms::generate::read_model_config(lookup_from({}));
  REQUIRE(!nothing.has_value());
  const auto& message = nothing.error().message;
  CHECK(message.find("CMSLEARN_MODEL_ENDPOINT") != std::string::npos);
  CHECK(message.find("CMSLEARN_MODEL_ID") != std::string::npos);
  CHECK(message.find("CMSLEARN_MODEL_KEY") != std::string::npos);
  CHECK(message.find("CMSLEARN_MODEL_PROMPT_VERSION") != std::string::npos);
  CHECK(message.find("CMSLEARN_DATABASE_URL") != std::string::npos);

  const auto partial = cms::generate::read_model_config(
      lookup_from({{"CMSLEARN_MODEL_ENDPOINT", "https://model.example.invalid"},
                   {"CMSLEARN_MODEL_KEY", "secret-that-must-not-appear"}}));
  REQUIRE(!partial.has_value());
  CHECK(partial.error().message.find("CMSLEARN_MODEL_ID") != std::string::npos);
  CHECK(partial.error().message.find("secret-that-must-not-appear") == std::string::npos);

  const auto complete = cms::generate::read_model_config(
      lookup_from({{"CMSLEARN_MODEL_ENDPOINT", "https://model.example.invalid"},
                   {"CMSLEARN_MODEL_ID", "m"},
                   {"CMSLEARN_MODEL_KEY", "k"},
                   {"CMSLEARN_MODEL_PROMPT_VERSION", "v1"},
                   {"CMSLEARN_DATABASE_URL", "postgres://x"}}));
  REQUIRE(complete.has_value());
  CHECK(complete->model_id == "m");
}
