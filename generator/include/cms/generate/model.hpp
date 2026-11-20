#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <cms/db/connection.hpp>
#include <cms/error.hpp>

namespace cms::generate {

/// @brief One completion, and the facts that make it reproducible: which model, which prompt version, which seed
/// and which call in the job. ADR 0028 keeps every one of those the deployer's configuration, and ADR 0015 needs
/// them recorded beside the artefact the call produced.
struct ModelCall {
  std::string model_id;
  std::string prompt_version;
  std::int64_t seed{0};
  std::int32_t call_index{0};
};

/// @brief What a model call produced.
struct Completion {
  std::string text;
  ModelCall call;
};

/// @brief The one way a generation step asks a model for text.
///
/// The product never carries a vendor, endpoint, model id, region or key of its own (ADR 0028): an implementation
/// is handed the deployer's configuration, and a test hands it recorded responses instead.
class ModelClient {
 public:
  virtual ~ModelClient() = default;

  /// @param prompt The text this step sends.
  /// @return The completion with its call facts, or the reason the call failed.
  [[nodiscard]] virtual Result<Completion> complete(std::string_view prompt) = 0;
};

/// @brief The model record an operator added and picked (ADR 0055): the endpoint, the protocol shape, the model id and the
/// sealed key, read from the database rather than from the process environment.
struct ModelRecord {
  std::string name;
  std::string base_url;
  std::string protocol;
  std::string model_id;
  std::string sealed_key;
};

/// @brief A client that answers from a recorded table, keyed by call order.
///
/// Every step's test runs through this: no network, no credential, and the same recording rules as the live
/// client, so a test cannot pass on a shape the product would not produce.
class RecordedClient final : public ModelClient {
 public:
  /// @param model_id The id the recording was made with, echoed into every call.
  /// @param prompt_version The prompt version the recording was made with.
  /// @param seed The seed the recording was made with.
  /// @param responses One response per call, in order.
  RecordedClient(std::string model_id, std::string prompt_version, std::int64_t seed,
                 std::vector<std::string> responses);

  [[nodiscard]] auto complete(std::string_view prompt) -> Result<Completion> override;

  /// @return The prompts this client was asked, in order, so a step's test can assert what it sent.
  [[nodiscard]] auto prompts() const -> const std::vector<std::string>& { return prompts_; }

 private:
  std::string model_id_;
  std::string prompt_version_;
  std::int64_t seed_{0};
  std::vector<std::string> responses_;
  std::vector<std::string> prompts_;
  std::int32_t call_index_{0};
};

/// @brief Reads the effective model record: the enabled default.
///
/// @param database The connection the worker already holds.
/// @return The record, or a refusal that names what an operator has to do — no enabled model record, or a record whose
/// key this deployment cannot open.
[[nodiscard]] auto read_effective_model(cms::db::connection& database) -> Result<ModelRecord>;

/// @brief Builds the client the record's protocol names.
///
/// The adapter is chosen from the record's own `protocol` value, so a second protocol is a new implementation and a row,
/// never a pipeline change. A protocol this build has no adapter for is refused by name, before any call is attempted.
///
/// @param record The effective record.
/// @param prompt_version The prompt version the deployment is running (ADR 0015 records it beside every artefact).
/// @param seed The seed every call of this job uses.
/// @return The client, or the reason no adapter could be built.
[[nodiscard]] auto make_model_client(const ModelRecord& record, std::string_view prompt_version, std::int64_t seed)
    -> Result<std::unique_ptr<ModelClient>>;

/// @brief A client that answers through the deployment's effective record: it reads the record on its first call, opens the
/// key with the deployment's sealer, and delegates to the adapter the record's protocol names.
///
/// The record is read at the call, not at process start, so an operator who disables the record stops the very next step
/// by name — the step that would have called it ends failed with that refusal attached — instead of needing a restart.
class RecordBackedClient final : public ModelClient {
 public:
  /// @param config The deployment's connection, read fresh for every job.
  /// @param prompt_version The prompt version this deployment runs.
  /// @param seed The seed every call of a job shares.
  RecordBackedClient(std::shared_ptr<const cms::db::connection_config> config, std::string prompt_version,
                     std::int64_t seed);

  [[nodiscard]] auto complete(std::string_view prompt) -> Result<Completion> override;

 private:
  std::shared_ptr<const cms::db::connection_config> config_;
  std::string prompt_version_;
  std::int64_t seed_{0};
  std::unique_ptr<ModelClient> adapter_;
};

/// @brief Builds the OpenAI-compatible chat adapter (generator/src/model_http.cpp).
///
/// @param record The effective record; its base address and model id become the request's URL and body fields.
/// @param key The unsealed key, handed over only by make_model_client.
/// @param prompt_version The prompt version, recorded beside every call.
/// @param seed The seed every call of this job uses.
/// @return The client, or the reason it could not be built.
[[nodiscard]] auto make_openai_chat_client(const ModelRecord& record, std::string_view key,
                                           std::string_view prompt_version, std::int64_t seed)
    -> Result<std::unique_ptr<ModelClient>>;

/// @brief The protocol value this build speaks: an OpenAI-compatible chat completions endpoint.
inline constexpr std::string_view OPENAI_CHAT_PROTOCOL = "openai_chat";

}  // namespace cms::generate
