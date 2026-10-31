#pragma once

#include <string>
#include <string_view>

#include <cms/error.hpp>

namespace cms::api {

/// @brief The operator-supplied facts this service needs before it can start.
///
/// These are the facts ADR 0004 keeps outside the database, plus the address a server cannot invent.
/// No member has a default: a service that starts on a guessed value is worse than one that stops and
/// says what it is missing.
struct HostConfig {
  std::string listen_address;
  std::string listen_port;
  std::string database_url;
  std::string seal_key;
  std::string bootstrap_admin;
  std::string tls_mode;
  std::string admin_socket;
  std::string admin_uid;
};

/// @brief Reads a `KEY=VALUE` file and validates every required fact.
///
/// @param path Filesystem path to the host file.
/// @return The configuration, or an error naming every key that is missing or malformed.
///
/// The message names every problem rather than the first one, so an operator fixes the file once
/// instead of restarting until the list empties. Values are never included in the message: the seal
/// key is in this file.
[[nodiscard]] Result<HostConfig> load_host_config(std::string_view path);

}  // namespace cms::api
