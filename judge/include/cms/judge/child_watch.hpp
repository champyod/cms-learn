#pragma once

#include <cstdint>

#include <sys/types.h>

namespace cms::judge {

/// @brief A child process held open by a pidfd, which the kernel binds to that one process for its lifetime.
///
/// A signal sent through the descriptor cannot reach a pid the kernel has already recycled, which is the race
/// a pid-based kill leaves open. The descriptor also becomes readable when the child exits, so waiting is a
/// block until that happens rather than a tick on a timer.
class child_process {
 public:
  /// Opens a descriptor for @p child. `is_open()` reports whether the kernel provided one.
  explicit child_process(pid_t child);
  ~child_process();

  child_process(const child_process&) = delete;
  child_process& operator=(const child_process&) = delete;
  child_process(child_process&&) = delete;
  child_process& operator=(child_process&&) = delete;

  /// @brief Whether a usable descriptor was obtained.
  [[nodiscard]] auto is_open() const -> bool { return descriptor_ >= 0; }
  /// @brief The pid this descriptor is bound to.
  [[nodiscard]] auto child() const -> pid_t { return child_; }
  /// @brief The descriptor itself, for a caller that polls it.
  [[nodiscard]] auto descriptor() const -> int { return descriptor_; }

  /// @brief Sends @p signal_number to this process alone, never to a process that inherited its pid.
  auto send(int signal_number) const -> bool;

 private:
  pid_t child_;
  int descriptor_;
};

/// @brief Waits up to @p milliseconds for @p child to exit.
/// @param status Receives the wait status when the child exits inside the deadline; untouched otherwise.
/// @return Whether the child exited before the deadline.
///
/// A child that ignores the deadline is left running: the caller decides whether to signal it and reap it,
/// because only the caller knows what it started.
[[nodiscard]] auto wait_within(const child_process& child, std::int64_t milliseconds,
                               int* status = nullptr) -> bool;

}  // namespace cms::judge
