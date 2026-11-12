#include <cms/generate/config.hpp>

#include <array>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace cms::generate {
namespace {

struct RequiredKey {
  std::string_view variable;
  std::string ModelConfig::*member;
};

constexpr auto REQUIRED_KEYS = std::array{
    RequiredKey{"CMSLEARN_MODEL_ENDPOINT", &ModelConfig::endpoint},
    RequiredKey{"CMSLEARN_MODEL_ID", &ModelConfig::model_id},
    RequiredKey{"CMSLEARN_MODEL_KEY", &ModelConfig::key},
    RequiredKey{"CMSLEARN_MODEL_PROMPT_VERSION", &ModelConfig::prompt_version},
    RequiredKey{"CMSLEARN_DATABASE_URL", &ModelConfig::database_url},
};

}  // namespace

auto environment_lookup() -> std::function<std::string(std::string_view)> {
  return [](std::string_view name) {
    const auto* value = std::getenv(std::string{name}.c_str());
    return value == nullptr ? std::string{} : std::string{value};
  };
}

auto read_model_config(const std::function<std::string(std::string_view)>& lookup)
    -> Result<ModelConfig> {
  auto config = ModelConfig{};
  auto missing = std::vector<std::string>{};
  for (const auto& key : REQUIRED_KEYS) {
    const auto value = lookup(key.variable);
    if (value.empty()) {
      missing.emplace_back(key.variable);
      continue;
    }
    config.*(key.member) = value;
  }
  if (!missing.empty()) {
    auto named = std::string{};
    for (const auto& key : missing) {
      if (!named.empty()) {
        named += ", ";
      }
      named += key;
    }
    return std::unexpected(Error::make(ErrorCode::invalid_argument,
                                       "the generator worker is missing its model configuration: " +
                                           named));
  }
  return config;
}

}  // namespace cms::generate
