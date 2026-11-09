#include <cms/api/csrf.hpp>

#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include <sodium.h>

#include <cms/api/uuid.hpp>

namespace cms::api {

auto CsrfTokens::token_for(std::string_view session_id) -> std::string {
  const auto guard = std::scoped_lock{mutex_};
  const auto found = tokens_.find(std::string{session_id});
  if (found != tokens_.end()) {
    return found->second;
  }
  const auto issued = tokens_.emplace(std::string{session_id}, uuid_v4());
  return issued.first->second;
}

auto CsrfTokens::is_token_for(std::string_view session_id, std::string_view token) const -> bool {
  const auto guard = std::scoped_lock{mutex_};
  const auto found = tokens_.find(std::string{session_id});
  // sodium_memcmp needs equal lengths; the length is not the secret, the bytes are.
  if (found == tokens_.end() || found->second.size() != token.size()) {
    return false;
  }
  return sodium_memcmp(found->second.data(), token.data(), token.size()) == 0;
}

}  // namespace cms::api
