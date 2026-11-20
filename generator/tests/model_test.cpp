// The model interface and its recorded stub: every step's test runs through these, with no network and no
// credential, and every artefact must carry the call it came from (model id, prompt version, seed, call index).

#include "live_database.hpp"

#include <cms/generate/config.hpp>
#include <cms/generate/model.hpp>
#include <cms/seal.hpp>

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <string_view>
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
                   {"CMSLEARN_DATABASE_URL", "postgres://x"},
                   {"CMSLEARN_CERTIFIER_DATABASE_URL", "postgres://certifier"},
                   {"CMSLEARN_S3_ENDPOINT", "https://store.example.invalid"},
                   {"CMSLEARN_S3_REGION", "us-east-1"},
                   {"CMSLEARN_S3_BUCKET", "b"},
                   {"CMSLEARN_S3_ACCESS_KEY_ID", "a"},
                   {"CMSLEARN_S3_SECRET_ACCESS_KEY", "s"},
                   {"CMSLEARN_CONTENT_ROOT", "/content"},
                   {"CMSLEARN_COMMIT", "a1b2c3d4e5f60718293a4b5c6d7e8f9012345678"}}));
  REQUIRE(complete.has_value());
  CHECK(complete->model_id == "m");
  CHECK(complete->store_bucket == "b");
  CHECK(complete->content_root == "/content");
  CHECK(complete->certifier_database_url == "postgres://certifier");
  CHECK(complete->commit_id == "a1b2c3d4e5f60718293a4b5c6d7e8f9012345678");
}

namespace {

constexpr auto TEST_SEAL_KEY = std::string_view{"0123456789abcdef0123456789abcdef"};
constexpr auto OTHER_SEAL_KEY = std::string_view{"fedcba9876543210fedcba9876543210"};

/// @return @p plaintext sealed with @p key, the way the page writes it.
auto sealed(std::string_view key, std::string_view plaintext) -> std::string {
  const auto sealer = cms::seal::Sealer::from_key(key);
  REQUIRE(sealer.has_value());
  const auto value = sealer->seal(plaintext);
  REQUIRE(value.has_value());
  return *value;
}

/// The stamp trigger reads the actor from the session, the same way every settings write does. One system account and one
/// session-scoped pair, set once per connection this test opens.
void given_system_actor(cms::db::connection& database) {
  database.execute_text("INSERT INTO system_accounts (id, handle) VALUES ('00000000-0000-4000-8000-000000000022', "
                        "'system:model-test') ON CONFLICT DO NOTHING", {});
  database.execute_text("SET cms.actor_kind = 'system'", {});
  database.execute_text("SET cms.actor_id = '00000000-0000-4000-8000-000000000022'", {});
}

void clear_models(cms::db::connection& database) {
  database.execute_text("DELETE FROM models", {});
}

}  // namespace

TEST_CASE("the effective model record is the enabled default, and a second record replaces it when picked", "[generate][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  REQUIRE(cms::seal::configure(TEST_SEAL_KEY).has_value());
  auto database = cms::generate::test::live_database();
  given_system_actor(database);
  clear_models(database);
  database.execute_text("INSERT INTO models (name, base_url, protocol, model_id, sealed_key, enabled, is_default, "
                        "actor_kind) VALUES ('first', 'https://model.invalid/v1', 'openai_chat', 'm-1', $1, true, true, "
                        "'system')",
                        {sealed(TEST_SEAL_KEY, "first-key")});

  const auto first = cms::generate::read_effective_model(database);
  REQUIRE(first.has_value());
  CHECK(first->name == "first");
  CHECK(first->model_id == "m-1");

  // A second record, and the operator picks it: the worker now reads it without a code change.
  database.execute_text("UPDATE models SET is_default = false WHERE name = 'first'", {});
  database.execute_text("INSERT INTO models (name, base_url, protocol, model_id, sealed_key, enabled, is_default, "
                        "actor_kind) VALUES ('second', 'https://other.invalid/v1', 'openai_chat', 'm-2', $1, true, true, "
                        "'system')",
                        {sealed(TEST_SEAL_KEY, "second-key")});

  const auto second = cms::generate::read_effective_model(database);
  REQUIRE(second.has_value());
  CHECK(second->name == "second");
  CHECK(second->model_id == "m-2");
  CHECK(second->base_url == "https://other.invalid/v1");

  // The client the second record builds is the one its protocol names, and it carries the record's model id.
  const auto client = cms::generate::make_model_client(*second, "prompt-v1", 7);
  REQUIRE(client.has_value());
  clear_models(database);
}

TEST_CASE("a second default is refused by the record itself", "[generate][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  REQUIRE(cms::seal::configure(TEST_SEAL_KEY).has_value());
  auto database = cms::generate::test::live_database();
  given_system_actor(database);
  clear_models(database);
  database.execute_text("INSERT INTO models (name, base_url, protocol, model_id, sealed_key, enabled, is_default, "
                        "actor_kind) VALUES ('only', 'https://model.invalid/v1', 'openai_chat', 'm', $1, true, true, "
                        "'system')",
                        {sealed(TEST_SEAL_KEY, "key")});
  auto refused = std::string{};
  try {
    database.execute_text("INSERT INTO models (name, base_url, protocol, model_id, sealed_key, enabled, is_default, "
                          "actor_kind) VALUES ('clash', 'https://model.invalid/v1', 'openai_chat', 'm', $1, true, "
                          "true, 'system')",
                          {sealed(TEST_SEAL_KEY, "key")});
  } catch (const std::exception& error) {
    refused = error.what();
  }
  CHECK(refused.find("models_one_default") != std::string::npos);
  clear_models(database);
}

TEST_CASE("an empty table is refused by name, and so is a protocol this build has no adapter for", "[generate][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  REQUIRE(cms::seal::configure(TEST_SEAL_KEY).has_value());
  auto database = cms::generate::test::live_database();
  given_system_actor(database);
  clear_models(database);

  const auto none = cms::generate::read_effective_model(database);
  REQUIRE(!none.has_value());
  CHECK(none.error().message.find("no enabled model record") != std::string::npos);
  CHECK(none.error().code == cms::ErrorCode::not_found);

  // A record whose protocol has no adapter is refused before a call is attempted, and the refusal names the protocol
  // and the record, never the key.
  const auto stranger = cms::generate::ModelRecord{.name = "stranger",
                                                   .base_url = "https://model.invalid/v1",
                                                   .protocol = "anthropic_messages",
                                                   .model_id = "m",
                                                   .sealed_key = sealed(TEST_SEAL_KEY, "the-key")};
  const auto client = cms::generate::make_model_client(stranger, "prompt-v1", 1);
  REQUIRE(!client.has_value());
  CHECK(client.error().message.find("anthropic_messages") != std::string::npos);
  CHECK(client.error().message.find("no adapter") != std::string::npos);
  CHECK(client.error().message.find("the-key") == std::string::npos);
}

TEST_CASE("a record sealed with another deployment's key is refused by name", "[generate][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  REQUIRE(cms::seal::configure(TEST_SEAL_KEY).has_value());
  auto database = cms::generate::test::live_database();
  given_system_actor(database);
  clear_models(database);
  database.execute_text("INSERT INTO models (name, base_url, protocol, model_id, sealed_key, enabled, is_default, "
                        "actor_kind) VALUES ('foreign', 'https://model.invalid/v1', 'openai_chat', 'm', $1, true, true, "
                        "'system')",
                        {sealed(OTHER_SEAL_KEY, "not-our-key")});

  const auto record = cms::generate::read_effective_model(database);
  REQUIRE(record.has_value());
  const auto client = cms::generate::make_model_client(*record, "prompt-v1", 1);
  REQUIRE(!client.has_value());
  CHECK(client.error().message.find("foreign") != std::string::npos);
  CHECK(client.error().message.find("cannot be opened") != std::string::npos);
  CHECK(client.error().message.find("not-our-key") == std::string::npos);
  clear_models(database);
}
