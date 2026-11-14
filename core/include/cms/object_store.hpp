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

/// @brief The content address of @p bytes: their SHA-256, in lowercase hexadecimal.
[[nodiscard]] auto digest_of(std::string_view bytes) -> std::string;

/// @return Whether @p digest has the shape an artefact row records: 64 lowercase hex digits.
[[nodiscard]] auto is_digest(std::string_view digest) -> bool;

/// @brief The one way this codebase reads and writes task bytes.
///
/// A key whose object does not exist is a refusal that names the key, never empty content: a reader that
/// treated a missing object as an empty file would seed a task with a blank statement and call it valid.
/// An artefact is content-addressed, so the read that serves one takes the digest its row records and hashes
/// what came back: bytes that no longer match their address are a refusal, not a different version of the same
/// object (ADR 0023 §2, ADR 0027 §2).
class ObjectStore {
 public:
  virtual ~ObjectStore() = default;

  /// @brief Reads the bytes at @p key for a row that records no digest of them.
  /// @param key The object's key, as the row stores it.
  /// @return The bytes, or `not_found` naming @p key, or `unavailable` with the store's reason.
  [[nodiscard]] virtual auto read(std::string_view key) const -> Result<std::string> = 0;

  /// @brief Reads the bytes whose content address @p digest names, refusing bytes that hash to something else.
  /// @param key The object's key, as the row stores it, named in every refusal.
  /// @param digest The digest the row records.
  /// @return The bytes, `not_found` naming @p key, `conflict` naming the key and both digests, or `unavailable`.
  [[nodiscard]] virtual auto read_verified(std::string_view key, std::string_view digest) const
      -> Result<std::string> = 0;

  /// @brief Writes @p bytes under @p key, so the next read can be checked against their address.
  /// @param key The object's key, which the caller records in the row beside the digest.
  /// @param bytes The object's content.
  /// @return The digest the caller must record, or the reason the store refused the write.
  [[nodiscard]] virtual auto write(std::string_view key, std::string_view bytes)
      -> Result<std::string> = 0;
};

/// @brief Builds the store client, or refuses when a value is missing.
///
/// @param config The endpoint, region, bucket and credential to use.
/// @return The client, or an error naming every missing value (never its opposite: the secret is here).
[[nodiscard]] Result<std::unique_ptr<ObjectStore>> make_store(const StoreConfig& config);

}  // namespace cms::store
