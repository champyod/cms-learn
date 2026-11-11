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
  /// Empty means the administrator panel is not mounted at all; Drogon keeps one route table per app,
  /// so a second listener is what separates the panel from the public site.
  std::string panel_listen_address;
  std::string panel_listen_port;
  std::string database_url;
  std::string seal_key;
  std::string bootstrap_admin;
  std::string tls_mode;
  std::string admin_socket;
  std::string admin_uid;
  std::string template_root;
  std::string static_root;
  /// The object store the task bytes live in (ADR 0023). All five are set together or none is: a store
  /// half-configured is a service that would read a task's bytes from nowhere.
  std::string s3_endpoint;
  std::string s3_region;
  std::string s3_bucket;
  std::string s3_access_key_id;
  std::string s3_secret_access_key;
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
