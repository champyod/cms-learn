#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

}  // namespace cms::generate
