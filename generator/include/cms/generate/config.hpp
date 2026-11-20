#pragma once

#include <functional>
#include <string>
#include <string_view>

#include <cms/error.hpp>

namespace cms::generate {

/// @brief The deployer's configuration (ADR 0028): the model's endpoint, id, key and prompt version; the database the
/// record is written to; the certifier's own connection; the object store every artefact's bytes go to; the content
/// root the vocabulary is read from;
/// and the commit the evidence is produced from, which the certificate records. No member has a default, because a
/// default chosen in our environment is a product that works where we work.
struct ModelConfig {
  std::string endpoint;
  std::string model_id;
  std::string key;
  std::string prompt_version;
  std::string database_url;
  /// The certifier's own role: the grants that decide a verification are its, not the generator's (ADR 0046).
  std::string certifier_database_url;
  std::string store_endpoint;
  std::string store_region;
  std::string store_bucket;
  std::string store_access_key_id;
  std::string store_secret_access_key;
  std::string content_root;
  std::string commit_id;
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
