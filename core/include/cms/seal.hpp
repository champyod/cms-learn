#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

#include <cms/error.hpp>

namespace cms::seal {

/// The bytes `CMSLEARN_SEAL_KEY` must carry. The deployment's key is the secret every sealed value is bound to.
inline constexpr std::size_t KEY_BYTES = 32;

/// Every sealed value starts with this, so a reader can tell a seal from plain text and a row can be checked for it.
inline constexpr std::string_view PREFIX = "sealed:v1:";

/// @brief Seals and unseals secrets with the deployment's key (ADR 0054 §4).
///
/// A sealed value is `sealed:v1:` then the per-value nonce and the box, both base64 in one ASCII string — one column
/// holds what it needs, and nothing outside the ciphertext says how to read it. The nonce is random for every value, so
/// sealing the same secret twice yields two different strings; reusing a nonce is the defect that makes two ciphertexts
/// of one key recoverable, which is why it is generated per call and never stored or derived from the value.
///
/// A refusal names the failure — not sealed, malformed, truncated, or not openable with this key — and never any part of
/// the value, the plaintext or the key.
class Sealer {
 public:
  /// @param key The deployment's key, exactly KEY_BYTES bytes.
  /// @return The sealer, or a refusal naming the length CMSLEARN_SEAL_KEY must have.
  [[nodiscard]] static auto from_key(std::string_view key) -> Result<Sealer>;

  /// @param plaintext The secret to seal.
  /// @return The sealed form, or the reason it could not be produced.
  [[nodiscard]] auto seal(std::string_view plaintext) const -> Result<std::string>;

  /// @param sealed A value in the form seal() produces.
  /// @return The plaintext, or a refusal naming the failure and nothing else.
  [[nodiscard]] auto unseal(std::string_view sealed) const -> Result<std::string>;

  /// @param key A candidate key.
  /// @return Whether it is the one this sealer holds, compared in constant time; it reveals nothing about the key.
  [[nodiscard]] auto matches_key(std::string_view key) const -> bool;

 private:
  explicit Sealer(const std::array<unsigned char, KEY_BYTES>& key);

  std::array<unsigned char, KEY_BYTES> key_;
};

/// @return Whether @p text is in the sealed form, so a reader can tell a seal from a plaintext value without the key.
[[nodiscard]] auto is_sealed(std::string_view text) -> bool;

/// @brief Configures the process's sealer from the deployment's key, once.
///
/// Called where the configuration is read, so the key is validated at start and held for the life of the process. A
/// second call with a different key is refused rather than silently replacing the one rows were sealed with.
/// @param key The deployment's key, exactly KEY_BYTES bytes.
///
/// @return Nothing, or the reason the key could not be configured.
[[nodiscard]] auto configure(std::string_view key) -> Result<void>;

/// @return A copy of the sealer this process was configured with (32 bytes), or a refusal naming CMSLEARN_SEAL_KEY when
/// it was not.
[[nodiscard]] auto sealer() -> Result<Sealer>;

}  // namespace cms::seal
