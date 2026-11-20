#include <cms/seal.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include <sodium.h>

namespace cms::seal {
namespace {

constexpr std::size_t NONCE_BYTES = crypto_secretbox_NONCEBYTES;
constexpr std::size_t MAC_BYTES = crypto_secretbox_MACBYTES;
constexpr int BASE64_VARIANT = sodium_base64_VARIANT_URLSAFE_NO_PADDING;
constexpr std::size_t NO_LIMIT = 0;

/// libsodium's initialiser is idempotent and thread-safe; it must run before any random or box call.
auto ensure_ready() -> bool { return sodium_init() >= 0; }

auto refuse(std::string_view message) -> Error {
  return Error::make(ErrorCode::invalid_argument, message);
}

/// @return @p bytes as base64 text, or nothing when the encoding failed.
auto to_base64(std::string_view bytes) -> std::optional<std::string> {
  auto encoded = std::string(sodium_base64_encoded_len(bytes.size(), BASE64_VARIANT), '\0');
  const auto* written = sodium_bin2base64(encoded.data(), encoded.size(), reinterpret_cast<const unsigned char*>(bytes.data()),
                                          bytes.size(), BASE64_VARIANT);
  if (written == nullptr) {
    return std::nullopt;
  }
  encoded.resize(std::string_view{written}.size());
  return encoded;
}

/// @return The bytes @p text encodes, or nothing when it is not base64 of that variant.
auto from_base64(std::string_view text) -> std::optional<std::string> {
  auto decoded = std::string(text.size(), '\0');
  auto length = std::size_t{0};
  if (sodium_base642bin(reinterpret_cast<unsigned char*>(decoded.data()), decoded.size(), text.data(), text.size(),
                        nullptr, &length, nullptr, BASE64_VARIANT) != 0) {
    return std::nullopt;
  }
  decoded.resize(length);
  return decoded;
}

/// @return The two parts a sealed value carries after its prefix, or nothing when its shape is wrong.
auto parts_of(std::string_view sealed) -> std::optional<std::pair<std::string_view, std::string_view>> {
  if (!is_sealed(sealed)) {
    return std::nullopt;
  }
  const auto body = sealed.substr(PREFIX.size());
  const auto separator = body.find(':');
  if (separator == std::string_view::npos || separator == 0 || separator + 1 >= body.size()) {
    return std::nullopt;
  }
  return std::pair{body.substr(0, separator), body.substr(separator + 1)};
}

/// The process's sealer, configured once where the deployment's configuration is read.
auto configured_sealer() -> std::optional<Sealer>& {
  static auto value = std::optional<Sealer>{};
  return value;
}

}  // namespace

auto is_sealed(std::string_view text) -> bool { return text.starts_with(PREFIX); }

auto Sealer::matches_key(std::string_view key) const -> bool {
  if (key.size() != KEY_BYTES) {
    return false;
  }
  return sodium_memcmp(key_.data(), key.data(), KEY_BYTES) == 0;
}

Sealer::Sealer(const std::array<unsigned char, KEY_BYTES>& key) : key_(key) {}

auto Sealer::from_key(std::string_view key) -> Result<Sealer> {
  if (!ensure_ready()) {
    return std::unexpected(Error::make(ErrorCode::unavailable, "libsodium did not initialise"));
  }
  if (key.size() != KEY_BYTES) {
    return std::unexpected(refuse("CMSLEARN_SEAL_KEY must be exactly " + std::to_string(KEY_BYTES) +
                                  " bytes, and it is " + std::to_string(key.size())));
  }
  auto bytes = std::array<unsigned char, KEY_BYTES>{};
  for (auto index = std::size_t{0}; index < KEY_BYTES; ++index) {
    bytes[index] = static_cast<unsigned char>(key[index]);
  }
  return Sealer{bytes};
}

auto Sealer::seal(std::string_view plaintext) const -> Result<std::string> {
  if (!ensure_ready()) {
    return std::unexpected(Error::make(ErrorCode::unavailable, "libsodium did not initialise"));
  }
  auto nonce = std::array<unsigned char, NONCE_BYTES>{};
  randombytes_buf(nonce.data(), nonce.size());
  auto box = std::string(plaintext.size() + MAC_BYTES, '\0');
  if (crypto_secretbox_easy(reinterpret_cast<unsigned char*>(box.data()),
                            reinterpret_cast<const unsigned char*>(plaintext.data()), plaintext.size(), nonce.data(),
                            key_.data()) != 0) {
    return std::unexpected(Error::make(ErrorCode::unavailable, "the value could not be sealed"));
  }
  const auto sealed_nonce = to_base64(std::string_view{reinterpret_cast<const char*>(nonce.data()), nonce.size()});
  const auto sealed_box = to_base64(box);
  if (!sealed_nonce || !sealed_box) {
    return std::unexpected(Error::make(ErrorCode::unavailable, "the sealed value could not be encoded"));
  }
  return std::string{PREFIX} + *sealed_nonce + ":" + *sealed_box;
}

auto Sealer::unseal(std::string_view sealed) const -> Result<std::string> {
  if (!ensure_ready()) {
    return std::unexpected(Error::make(ErrorCode::unavailable, "libsodium did not initialise"));
  }
  if (!is_sealed(sealed)) {
    return std::unexpected(refuse("the value is not a sealed secret"));
  }
  const auto parts = parts_of(sealed);
  if (!parts) {
    return std::unexpected(refuse("the sealed value is malformed: it does not carry a nonce and a box"));
  }
  const auto nonce = from_base64(parts->first);
  const auto box = from_base64(parts->second);
  if (!nonce || !box) {
    return std::unexpected(refuse("the sealed value is malformed: its nonce or its box is not base64"));
  }
  if (nonce->size() != NONCE_BYTES) {
    return std::unexpected(refuse("the sealed value is malformed: its nonce is not " +
                                  std::to_string(NONCE_BYTES) + " bytes"));
  }
  if (box->size() <= MAC_BYTES) {
    return std::unexpected(refuse("the sealed value is truncated: it is shorter than the box it must carry"));
  }
  auto plaintext = std::string(box->size() - MAC_BYTES, '\0');
  if (crypto_secretbox_open_easy(reinterpret_cast<unsigned char*>(plaintext.data()),
                                 reinterpret_cast<const unsigned char*>(box->data()), box->size(),
                                 reinterpret_cast<const unsigned char*>(nonce->data()), key_.data()) != 0) {
    return std::unexpected(refuse("the sealed value cannot be opened with this deployment's key"));
  }
  return plaintext;
}

auto configure(std::string_view key) -> Result<void> {
  const auto candidate = Sealer::from_key(key);
  if (!candidate) {
    return std::unexpected(candidate.error());
  }
  auto& current = configured_sealer();
  if (current.has_value()) {
    // The key is configured once. A second, different key would make every row sealed under the first unreadable, so the
    // process refuses rather than replacing it, and the comparison is constant-time over the bytes.
    if (!current->matches_key(key)) {
      return std::unexpected(Error::make(ErrorCode::conflict,
                                         "CMSLEARN_SEAL_KEY changed after it was configured, so every value sealed with "
                                         "the first key would no longer open"));
    }
    return {};
  }
  current = *candidate;
  return {};
}

auto sealer() -> Result<Sealer> {
  const auto& current = configured_sealer();
  if (!current.has_value()) {
    // A process without a key has no sealer; the alternative is a default key, and a default key is a product that works
    // where we work.
    return std::unexpected(Error::make(ErrorCode::unavailable,
                                       "CMSLEARN_SEAL_KEY is not configured in this process, so no secret can be sealed "
                                       "or opened"));
  }
  return *current;
}

}  // namespace cms::seal
