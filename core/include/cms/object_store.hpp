#pragma once

#include <memory>
#include <string>
#include <string_view>

#include <cms/error.hpp>

namespace cms::store {

/// @brief Where the store lives, and the identity allowed to reach it.
///
/// No member has a default: a store addressed by a guessed bucket is worse than one that refuses.
struct StoreConfig {
  std::string endpoint;
  std::string region;
  std::string bucket;
  std::string access_key_id;
  std::string secret_access_key;
};

/// @brief The one way this codebase reads task bytes.
///
/// A key whose object does not exist is a refusal that names the key, never empty content: a reader that
/// treated a missing object as an empty file would seed a task with a blank statement and call it valid.
class ObjectStore {
 public:
  virtual ~ObjectStore() = default;

  /// @param key The object's key, as the row stores it.
  /// @return The bytes, or `not_found` naming @p key, or `unavailable` with the store's reason.
  [[nodiscard]] virtual Result<std::string> read(std::string_view key) const = 0;
};

/// @brief Builds the store client, or refuses when a value is missing.
///
/// @param config The endpoint, region, bucket and credential to use.
/// @return The client, or an error naming every missing value (never its opposite: the secret is here).
[[nodiscard]] Result<std::unique_ptr<ObjectStore>> make_store(const StoreConfig& config);

}  // namespace cms::store
