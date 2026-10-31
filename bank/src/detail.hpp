#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include <sodium.h>
#include <simdjson.h>

#include <cms/error.hpp>

namespace cms::bank::detail {

constexpr std::size_t HEX_DIGITS_PER_BYTE = 2;
constexpr unsigned NIBBLE_SHIFT = 4;
constexpr unsigned NIBBLE_MASK = 0x0fu;

/// @brief A running SHA-256, so a task's digest is fed file by file instead of by building one string.
class Hasher {
 public:
  Hasher() { crypto_hash_sha256_init(&state_); }

  void add(std::string_view bytes) {
    crypto_hash_sha256_update(&state_, reinterpret_cast<const unsigned char*>(bytes.data()),
                              bytes.size());
  }

  /// @brief Adds a field and a terminator, so ["ab", "c"] and ["a", "bc"] cannot hash the same.
  void add_field(std::string_view bytes) {
    add(bytes);
    add(std::string_view{"\0", 1});
  }

  [[nodiscard]] auto digest() -> std::string {
    auto bytes = std::array<unsigned char, crypto_hash_sha256_BYTES>{};
    crypto_hash_sha256_final(&state_, bytes.data());
    constexpr std::string_view DIGITS = "0123456789abcdef";
    auto text = std::string{};
    text.reserve(bytes.size() * HEX_DIGITS_PER_BYTE);
    for (const auto byte : bytes) {
      text.push_back(DIGITS[byte >> NIBBLE_SHIFT]);
      text.push_back(DIGITS[byte & NIBBLE_MASK]);
    }
    return text;
  }

 private:
  crypto_hash_sha256_state state_{};
};

/// @brief Reads a file whole, or says which file is missing.
[[nodiscard]] inline auto read_text(const std::filesystem::path& path) -> Result<std::string> {
  auto stream = std::ifstream{path, std::ios::binary};
  if (!stream) {
    return std::unexpected(Error::make(
        ErrorCode::not_found, "cannot read " + path.string() + "; the bank is the files"));
  }
  auto buffer = std::ostringstream{};
  buffer << stream.rdbuf();
  return buffer.str();
}

/// @brief Parses a JSON file and hands the document to @p use while the parser is still alive.
///
/// A `dom::element` points into the parser's buffer, so returning one from a helper would return a
/// dangling view. Taking a callback makes the lifetime a property of the signature.
template <class Use>
auto with_json(const std::filesystem::path& path, Use&& use) -> Result<void> {
  const auto text = read_text(path);
  if (!text) {
    return std::unexpected(text.error());
  }
  const auto json = simdjson::padded_string{*text};
  auto parser = simdjson::dom::parser{};
  auto document = simdjson::dom::element{};
  if (parser.parse(json).get(document) != simdjson::SUCCESS) {
    return std::unexpected(
        Error::make(ErrorCode::invalid_argument, path.string() + ": not valid JSON"));
  }
  auto object = simdjson::dom::object{};
  if (document.get_object().get(object) != simdjson::SUCCESS) {
    return std::unexpected(
        Error::make(ErrorCode::invalid_argument, path.string() + ": not a JSON object"));
  }
  return use(object);
}

[[nodiscard]] inline auto field_text(const simdjson::dom::object& object, std::string_view name)
    -> std::optional<std::string> {
  auto view = std::string_view{};
  if (object.at_key(name).get_string().get(view) != simdjson::SUCCESS) {
    return std::nullopt;
  }
  return std::string{view};
}

[[nodiscard]] inline auto field_integer(const simdjson::dom::object& object, std::string_view name)
    -> std::optional<std::int64_t> {
  auto value = std::int64_t{0};
  if (object.at_key(name).get_int64().get(value) != simdjson::SUCCESS) {
    return std::nullopt;
  }
  return value;
}

/// @brief Builds a refusal that names the file or task it came from.
[[nodiscard]] inline auto fail(std::string_view where, std::string_view text) -> Error {
  return Error::make(ErrorCode::invalid_argument, std::string{where} + ": " + std::string{text});
}

/// @brief Reads a nested object, or nothing when the key is absent or holds another type.
[[nodiscard]] inline auto nested(const simdjson::dom::object& object, std::string_view name)
    -> std::optional<simdjson::dom::object> {
  auto child = simdjson::dom::object{};
  if (object.at_key(name).get_object().get(child) != simdjson::SUCCESS) {
    return std::nullopt;
  }
  return child;
}

[[nodiscard]] inline auto required_text(const simdjson::dom::object& object, std::string_view name,
                                        std::string_view where) -> Result<std::string> {
  const auto value = field_text(object, name);
  if (!value || value->empty()) {
    return std::unexpected(Error::make(
        ErrorCode::invalid_argument, std::string{where} + ": " + std::string{name} + " is missing"));
  }
  return *value;
}

[[nodiscard]] inline auto required_integer(const simdjson::dom::object& object,
                                           std::string_view name, std::string_view where)
    -> Result<std::int64_t> {
  const auto value = field_integer(object, name);
  if (!value) {
    return std::unexpected(Error::make(
        ErrorCode::invalid_argument, std::string{where} + ": " + std::string{name} + " is missing"));
  }
  return *value;
}

template <class Allowed>
[[nodiscard]] auto known(std::string_view value, const Allowed& allowed, std::string_view name,
                         std::string_view where) -> Result<void> {
  for (const auto& candidate : allowed) {
    if (candidate == value) {
      return {};
    }
  }
  return std::unexpected(Error::make(
      ErrorCode::invalid_argument,
      std::string{where} + ": " + std::string{name} + " is \"" + std::string{value} +
          "\", which the schema does not define"));
}

}  // namespace cms::bank::detail
