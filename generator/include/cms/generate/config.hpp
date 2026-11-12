#pragma once

#include <functional>
#include <string>
#include <string_view>

#include <cms/error.hpp>

namespace cms::generate {

/// @brief The deployer's model configuration (ADR 0028): the endpoint, the model id, the key and the prompt
/// version. No member has a default, because a default chosen in our environment is a product that works where
/// we work.
struct ModelConfig {
  std::string endpoint;
  std::string model_id;
  std::string key;
  std::string prompt_version;
  std::string database_url;
};

/// @brief Reads the worker's configuration from the environment.
///
/// @param lookup Reads one variable; injected so a test can supply a table instead of a process environment.
/// @return The configuration, or an error naming every missing key — never its value, because the key is here.
[[nodiscard]] auto read_model_config(const std::function<std::string(std::string_view)>& lookup)
    -> Result<ModelConfig>;

/// @brief The process environment as the lookup above expects it.
[[nodiscard]] auto environment_lookup() -> std::function<std::string(std::string_view)>;

}  // namespace cms::generate
