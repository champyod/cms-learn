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
    RequiredKey{"CMSLEARN_DATABASE_URL", &ModelConfig::database_url},
    RequiredKey{"CMSLEARN_CERTIFIER_DATABASE_URL", &ModelConfig::certifier_database_url},
    RequiredKey{"CMSLEARN_S3_ENDPOINT", &ModelConfig::store_endpoint},
    RequiredKey{"CMSLEARN_S3_REGION", &ModelConfig::store_region},
    RequiredKey{"CMSLEARN_S3_BUCKET", &ModelConfig::store_bucket},
    RequiredKey{"CMSLEARN_S3_ACCESS_KEY_ID", &ModelConfig::store_access_key_id},
    RequiredKey{"CMSLEARN_S3_SECRET_ACCESS_KEY", &ModelConfig::store_secret_access_key},
    RequiredKey{"CMSLEARN_CONTENT_ROOT", &ModelConfig::content_root},
    RequiredKey{"CMSLEARN_COMMIT", &ModelConfig::commit_id},
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
                                       "the generator worker is missing its configuration: " + named));
  }
  return config;
}

}  // namespace cms::generate
