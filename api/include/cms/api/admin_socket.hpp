#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <sys/types.h>
#include <thread>

#include <cms/db/connection.hpp>
#include <cms/settings_snapshot.hpp>

namespace cms::api {

/// @brief The administrative channel: a Unix socket that never touches the network.
///
/// Drogon serves TCP only, so this listener is ours. It speaks one JSON object per line in and one
/// out — no HTTP, because the only client is `cmsctl`, which we also write, and a protocol we do not
/// need is a parser nobody should have to secure.
///
/// The gate is the socket itself: mode 0600, and `SO_PEERCRED` checked on every accept so a process
/// that managed to reach the path still has to be the configured user.
class AdminSocket {
 public:
  /// @param path Absolute path to bind. An existing socket file is replaced.
  /// @param allowed_uid The only user id whose requests are answered.
  /// @param store The settings snapshot answered against.
  /// @param database_config The pool an action reads or writes through. Held as the pool rather than
  /// as a connection because a connection is move-only and each request builds its own from this; a
  /// borrowed reference would outlive its owner the moment the service restarts its listener.
  AdminSocket(std::string path, uid_t allowed_uid, std::shared_ptr<SettingsStore> store,
              std::shared_ptr<const db::connection_config> database_config);

  ~AdminSocket();

  AdminSocket(const AdminSocket&) = delete;
  AdminSocket& operator=(const AdminSocket&) = delete;
  AdminSocket(AdminSocket&&) = delete;
  AdminSocket& operator=(AdminSocket&&) = delete;

  /// @brief Binds the socket and starts accepting.
  /// @pre The socket has not already been started; asserting is the caller's contract.
  void start();

  /// @brief Stops the accept loop and removes the socket file.
  void stop();

 private:
  std::string path_;
  uid_t allowed_uid_;
  std::shared_ptr<SettingsStore> store_;
  std::shared_ptr<const db::connection_config> database_config_;
  std::atomic<bool> running_{false};
  std::thread worker_;
  int listen_fd_{-1};
};

}  // namespace cms::api
