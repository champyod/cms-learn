#include <cms/generate/model.hpp>

#include <memory>
#include <string>
#include <utility>

namespace cms::generate {

RecordedClient::RecordedClient(std::string model_id, std::string prompt_version, std::int64_t seed,
                               std::vector<std::string> responses)
    : model_id_(std::move(model_id)),
      prompt_version_(std::move(prompt_version)),
      seed_(seed),
      responses_(std::move(responses)) {}

auto RecordedClient::complete(std::string_view prompt) -> Result<Completion> {
  prompts_.emplace_back(prompt);
  if (static_cast<std::size_t>(call_index_) >= responses_.size()) {
    return std::unexpected(Error::make(
        ErrorCode::unavailable,
        "the recording holds " + std::to_string(responses_.size()) + " response(s) and this is call " +
            std::to_string(call_index_ + 1)));
  }
  auto call = ModelCall{.model_id = model_id_,
                        .prompt_version = prompt_version_,
                        .seed = seed_,
                        .call_index = call_index_};
  ++call_index_;
  return Completion{.text = responses_[static_cast<std::size_t>(call.call_index)],
                    .call = std::move(call)};
}

}  // namespace cms::generate
