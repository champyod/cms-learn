#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>

namespace cms {

/// @brief The failure classes a caller can act on differently.
///
/// Anything a caller cannot act on differently belongs in one of these rather than in a new member,
/// so a handler cannot grow a branch per message.
enum class ErrorCode : std::uint8_t {
  invalid_argument,
  not_found,
  permission_denied,
  conflict,
  unavailable,
  internal,
};

/// @brief A failure a caller can act on: a class it can branch on and a message it can log.
struct Error {
  ErrorCode code{ErrorCode::internal};
  std::string message;

  /// @brief Builds an error from a class and a message.
  [[nodiscard]] static Error make(ErrorCode code, std::string_view text) {
    return Error{code, std::string(text)};
  }
};

/// @brief Every fallible core operation returns this.
///
/// std::expected keeps the success path free of exceptions, so a request that fails on validation is
/// still ordinary control flow.
template <class T>
using Result = std::expected<T, Error>;

}  // namespace cms
