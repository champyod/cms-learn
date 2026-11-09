#pragma once

#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace cms::api {

/// @brief The tokens the administrator panel renders into its forms, one per session.
///
/// A token is issued once for a session and compared in constant time on every POST, so a page rendered
/// for one browser cannot be submitted from another: a request that carries no token, a token from a
/// different session, or no session at all is refused.
class CsrfTokens {
 public:
  /// @brief The token issued to @p session_id, creating one on first use.
  [[nodiscard]] auto token_for(std::string_view session_id) -> std::string;

  /// @brief Whether @p token is the one issued to @p session_id.
  [[nodiscard]] auto is_token_for(std::string_view session_id, std::string_view token) const -> bool;

 private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, std::string> tokens_;
};

}  // namespace cms::api
