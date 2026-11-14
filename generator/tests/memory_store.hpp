#pragma once

#include <map>
#include <string>
#include <string_view>

#include <cms/error.hpp>
#include <cms/object_store.hpp>

namespace cms::generate::test {

/// @brief A store that keeps its objects in a map, so a step's write and read are proven with no network.
///
/// It answers the same contract the client does: a missing key is `not_found` naming it, and a read with a
/// digest hashes what it holds before returning it. `overwrite` is the fault the digest exists for.
class MemoryStore final : public cms::store::ObjectStore {
 public:
  [[nodiscard]] auto read(std::string_view key) const -> cms::Result<std::string> override {
    const auto found = objects_.find(std::string{key});
    if (found == objects_.end()) {
      return std::unexpected(
          cms::Error::make(cms::ErrorCode::not_found, "no object at key " + std::string{key}));
    }
    return found->second;
  }

  [[nodiscard]] auto read_verified(std::string_view key, std::string_view digest) const
      -> cms::Result<std::string> override {
    verified_digest_ = std::string{digest};
    const auto bytes = read(key);
    if (!bytes) {
      return bytes;
    }
    const auto stored = cms::store::digest_of(*bytes);
    if (stored != digest) {
      return std::unexpected(cms::Error::make(
          cms::ErrorCode::conflict, "the object at key " + std::string{key} +
                                        " does not match the digest its row records: the row says " +
                                        std::string{digest} + ", the stored bytes hash to " + stored));
    }
    return bytes;
  }

  [[nodiscard]] auto write(std::string_view key, std::string_view bytes)
      -> cms::Result<std::string> override {
    objects_[std::string{key}] = std::string{bytes};
    return cms::store::digest_of(bytes);
  }

  /// @brief Replaces an object's bytes without touching the digest any row records.
  auto overwrite(std::string_view key, std::string_view bytes) -> void {
    objects_[std::string{key}] = std::string{bytes};
  }

  /// @return The bytes stored under @p key, empty when it holds none.
  [[nodiscard]] auto bytes_at(std::string_view key) const -> std::string {
    const auto found = objects_.find(std::string{key});
    return found == objects_.end() ? std::string{} : found->second;
  }

  /// @return The digest the last verifying read was given, so a test can assert the row's digest was passed.
  [[nodiscard]] auto verified_digest() const -> const std::string& { return verified_digest_; }

 private:
  std::map<std::string, std::string> objects_;
  mutable std::string verified_digest_;
};

}  // namespace cms::generate::test
