#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include <cms/db/connection.hpp>
#include <cms/settings_snapshot.hpp>

namespace cms::api {

/// @brief Keeps a SettingsStore current by two independent means.
///
/// The listener reacts to `pg_notify` in milliseconds; the timer reads the version counter every
/// @p poll_interval_seconds and reloads when it moved. Two mechanisms rather than one, because a
/// dropped notification and a dropped connection look identical from inside a process that only
/// listens — and a settings change that silently never applies is the failure this exists to prevent.
class SettingsWatcher {
 public:
  /// @param database The connector used for reloads.
  /// @param store The store this watcher publishes into.
  /// @param connection_string A libpq connection string used only for `LISTEN`.
  /// @param poll_interval_seconds How often the version counter is re-read as a backstop.
  SettingsWatcher(db::connection& database, std::shared_ptr<SettingsStore> store,
                  std::string connection_string, unsigned poll_interval_seconds);

  ~SettingsWatcher();

  SettingsWatcher(const SettingsWatcher&) = delete;
  SettingsWatcher& operator=(const SettingsWatcher&) = delete;
  SettingsWatcher(SettingsWatcher&&) = delete;
  SettingsWatcher& operator=(SettingsWatcher&&) = delete;

  /// @brief Starts the watcher thread.
  /// @pre The watcher has not already been started; asserting is the caller's contract.
  void start();

  /// @brief Stops and joins the watcher thread. Safe to call when it was never started.
  void stop();

 private:
  db::connection& database_;
  std::shared_ptr<SettingsStore> store_;
  std::string connection_string_;
  unsigned poll_interval_seconds_;
  std::atomic<bool> running_{false};
  std::thread worker_;
};

}  // namespace cms::api
