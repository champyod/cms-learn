#include <cms/judge/child_watch.hpp>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <limits>

#include <poll.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

namespace cms::judge {
namespace {

/// Opens a pidfd for @p child, or returns a negative value when the kernel does not provide one.
///
/// Called through `syscall` rather than `pidfd_open`: glibc declares that function with C++ linkage instead
/// of `extern "C"`, so the declared call links against a mangled name no C library exports.
auto open_process_descriptor(pid_t child) -> int {
  return static_cast<int>(::syscall(SYS_pidfd_open, child, 0));
}

/// Signals the process @p descriptor refers to. The kernel resolves the pid once, so a recycled pid can
/// never receive this signal.
auto signal_process_descriptor(int descriptor, int signal_number) -> bool {
  return ::syscall(SYS_pidfd_send_signal, descriptor, signal_number, nullptr, 0) == 0;
}

auto now() -> std::chrono::steady_clock::time_point { return std::chrono::steady_clock::now(); }

}  // namespace

child_process::child_process(pid_t child)
    : child_(child), descriptor_(open_process_descriptor(child)) {}

child_process::~child_process() {
  if (descriptor_ >= 0) {
    ::close(descriptor_);
  }
}

auto child_process::send(int signal_number) const -> bool {
  return signal_process_descriptor(descriptor_, signal_number);
}

auto wait_within(const child_process& child, std::int64_t milliseconds, int* status) -> bool {
  const auto deadline = now() + std::chrono::milliseconds{milliseconds};
  auto reaped = 0;
  auto& target = status == nullptr ? reaped : *status;
  while (true) {
    const auto seen = ::waitpid(child.child(), &target, WNOHANG);
    if (seen == child.child() || (seen < 0 && errno != EINTR)) {
      return true;
    }
    const auto remaining = deadline - now();
    if (remaining <= std::chrono::steady_clock::duration::zero()) {
      return false;
    }
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count();
    const auto timeout =
        static_cast<int>(std::min(left, static_cast<std::int64_t>(std::numeric_limits<int>::max())));
    auto watch = ::pollfd{.fd = child.descriptor(), .events = POLLIN, .revents = 0};
    if (::poll(&watch, 1, timeout) < 0 && errno != EINTR) {
      return false;
    }
  }
}

}  // namespace cms::judge
