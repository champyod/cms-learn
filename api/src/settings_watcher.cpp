#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <sys/select.h>
#include <thread>
#include <utility>

#include <libpq-fe.h>

#include <cms/api/settings_source.hpp>
#include <cms/api/settings_watcher.hpp>

namespace cms::api {
namespace {

constexpr std::string_view CHANNEL = "cms_settings";

struct connection_deleter {
  void operator()(PGconn* connection) const { PQfinish(connection); }
};

using connection_ptr = std::unique_ptr<PGconn, connection_deleter>;

auto connect(const std::string& dsn) -> connection_ptr {
  return connection_ptr{PQconnectdb(dsn.c_str())};
}

auto is_usable(PGconn* connection) -> bool {
  return connection != nullptr && PQstatus(connection) == CONNECTION_OK;
}

auto listen(PGconn* connection) -> bool {
  const auto statement = "LISTEN " + std::string{CHANNEL};
  auto* result = PQexec(connection, statement.c_str());
  if (result == nullptr) {
    return false;
  }
  const auto ok = PQresultStatus(result) == PGRES_COMMAND_OK;
  PQclear(result);
  return ok;
}

/// Waits up to `timeout` for the socket to become readable.
auto wait_readable(PGconn* connection, std::chrono::seconds timeout) -> bool {
  const auto socket = PQsocket(connection);
  if (socket < 0) {
    return false;
  }
  auto readable = fd_set{};
  FD_ZERO(&readable);
  FD_SET(socket, &readable);

  auto wait = timeval{};
  wait.tv_sec = static_cast<decltype(wait.tv_sec)>(timeout.count());
  wait.tv_usec = 0;

  const auto ready = ::select(socket + 1, &readable, nullptr, nullptr, &wait);
  return ready > 0 && FD_ISSET(socket, &readable);
}

}  // namespace

SettingsWatcher::SettingsWatcher(db::connection& database, std::shared_ptr<SettingsStore> store,
                                 std::string connection_string, unsigned poll_interval_seconds)
    : database_(database),
      store_(std::move(store)),
      connection_string_(std::move(connection_string)),
      poll_interval_seconds_(poll_interval_seconds) {}

SettingsWatcher::~SettingsWatcher() { stop(); }

auto SettingsWatcher::start() -> void {
  running_ = true;
  worker_ = std::thread([this] {
    auto connection = connection_ptr{};
    const auto poll_every = std::chrono::seconds{poll_interval_seconds_};
    auto last_check = std::chrono::steady_clock::now();

    while (running_.load(std::memory_order_relaxed)) {
      if (!is_usable(connection.get())) {
        connection = connect(connection_string_);
        if (!is_usable(connection.get()) || !listen(connection.get())) {
          connection.reset();
          std::this_thread::sleep_for(std::chrono::seconds{1});
          continue;
        }
      }

      if (wait_readable(connection.get(), std::chrono::seconds{1})) {
        if (PQconsumeInput(connection.get()) != 1) {
          connection.reset();
          continue;
        }
        auto reload = false;
        while (auto* notification = PQnotifies(connection.get())) {
          reload = true;
          PQfreemem(notification);
        }
        if (reload) {
          if (const auto loaded = load_settings(database_)) {
            store_->publish(*loaded);
          }
          last_check = std::chrono::steady_clock::now();
          continue;
        }
      }

      // The backstop: a notification that never arrived still shows up as a version that moved.
      const auto now = std::chrono::steady_clock::now();
      if (now - last_check < poll_every) {
        continue;
      }
      last_check = now;
      if (const auto loaded = load_settings(database_)) {
        store_->publish(*loaded);
      }
    }
  });
}

auto SettingsWatcher::stop() -> void {
  running_ = false;
  if (worker_.joinable()) {
    worker_.join();
  }
}

}  // namespace cms::api
