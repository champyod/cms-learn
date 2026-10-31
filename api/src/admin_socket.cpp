#include <cms/api/admin_socket.hpp>

#include <array>
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <poll.h>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <utility>

#include <cms/api/admin_protocol.hpp>

namespace cms::api {
namespace {

constexpr std::size_t MAX_REQUEST_BYTES = 4096;
constexpr int ACCEPT_POLL_MILLISECONDS = 200;
constexpr mode_t SOCKET_MODE = 0600;

auto write_all(int fd, std::string_view text) -> void {
  auto written = std::size_t{0};
  while (written < text.size()) {
    const auto count = ::write(fd, text.data() + written, text.size() - written);
    if (count <= 0) {
      return;
    }
    written += static_cast<std::size_t>(count);
  }
}


auto peer_uid(int fd) -> uid_t {
  auto credentials = ucred{};
  auto length = socklen_t{sizeof(credentials)};
  if (::getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &length) != 0) {
    return static_cast<uid_t>(-1);
  }
  return credentials.uid;
}

auto read_into(int fd, std::span<char> buffer) -> std::span<const char> {
  auto used = std::size_t{0};
  while (used < buffer.size()) {
    const auto count = ::read(fd, buffer.data() + used, buffer.size() - used);
    if (count <= 0) {
      break;
    }
    used += static_cast<std::size_t>(count);
    if (std::string_view{buffer.data(), used}.find('\n') != std::string_view::npos) {
      break;
    }
  }
  return {buffer.data(), used};
}

auto read_line(int fd) -> std::string {
  auto buffer = std::array<char, MAX_REQUEST_BYTES>{};
  const auto received = read_into(fd, buffer);
  return std::string{received.data(), received.size()};
}

/// Pulls the request line and hands it to the protocol. Nothing is interpreted here: the socket's one
/// job is to carry bytes for a peer the credentials already authorized.
auto answer(std::string_view request, const SettingsStore& store,
            const std::shared_ptr<const db::connection_config>& database_config) -> std::string {
  const auto snapshot = store.load();
  if (!snapshot) {
    return "{\"ok\":false,\"error\":\"no settings snapshot is loaded\"}\n";
  }
  auto database = db::connection{database_config};
  return dispatch_line(request, *snapshot, database);
}

}  // namespace

AdminSocket::AdminSocket(std::string path, uid_t allowed_uid, std::shared_ptr<SettingsStore> store,
                         std::shared_ptr<const db::connection_config> database_config)
    : path_(std::move(path)),
      allowed_uid_(allowed_uid),
      store_(std::move(store)),
      database_config_(std::move(database_config)) {}

AdminSocket::~AdminSocket() { stop(); }

auto AdminSocket::start() -> void {
  assert(listen_fd_ < 0);
  ::unlink(path_.c_str());

  listen_fd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (listen_fd_ < 0) {
    throw std::runtime_error("cannot create the admin socket");
  }

  auto address = sockaddr_un{};
  address.sun_family = AF_UNIX;
  if (path_.size() >= sizeof(address.sun_path)) {
    throw std::runtime_error("the admin socket path is too long for sockaddr_un");
  }
  std::memcpy(address.sun_path, path_.c_str(), path_.size() + 1);

  if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    throw std::runtime_error("cannot bind the admin socket at " + path_ + ": " + std::strerror(errno));
  }
  if (::chmod(path_.c_str(), SOCKET_MODE) != 0) {
    throw std::runtime_error("cannot restrict the admin socket to 0600");
  }
  if (::listen(listen_fd_, 8) != 0) {
    throw std::runtime_error("cannot listen on the admin socket");
  }

  running_ = true;
  worker_ = std::thread([this] {
    while (running_.load(std::memory_order_relaxed)) {
      auto waiting = pollfd{.fd = listen_fd_, .events = POLLIN, .revents = 0};
      if (::poll(&waiting, 1, ACCEPT_POLL_MILLISECONDS) <= 0) {
        continue;
      }
      const auto client = ::accept4(listen_fd_, nullptr, nullptr, SOCK_CLOEXEC);
      if (client < 0) {
        continue;
      }
      if (peer_uid(client) != allowed_uid_) {
        ::close(client);
        continue;
      }
      const auto request = read_line(client);
      write_all(client, answer(request, *store_, database_config_));
      ::close(client);
    }
  });
}

auto AdminSocket::stop() -> void {
  running_ = false;
  if (worker_.joinable()) {
    worker_.join();
  }
  if (listen_fd_ >= 0) {
    ::close(listen_fd_);
    listen_fd_ = -1;
  }
  ::unlink(path_.c_str());
}

}  // namespace cms::api
