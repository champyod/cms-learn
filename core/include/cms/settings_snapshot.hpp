#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <version>

#if !defined(__cpp_lib_atomic_shared_ptr)
#error "cms::SettingsStore needs std::atomic<std::shared_ptr<const T>>; this standard library lacks it"
#endif

namespace cms {

/// @brief A settings value in the shape it is stored: the three types a settings row can hold.
using SettingValue = std::variant<bool, std::int64_t, std::string>;

/// @brief An immutable view of every settings row.
///
/// std::less<> makes the lookup heterogeneous, so reading a setting by string_view neither allocates
/// nor copies. The map is immutable once published, which is what makes handing a pointer into it to
/// a request handler safe for the snapshot's whole lifetime.
struct SettingsSnapshot {
  std::int64_t version{0};
  std::map<std::string, SettingValue, std::less<>> values;

  /// @brief The value for @p key, or nullptr when the key is absent.
  [[nodiscard]] const SettingValue* find(std::string_view key) const noexcept {
    const auto it = values.find(key);
    return it == values.end() ? nullptr : &it->second;
  }
};

/// @brief Reads a setting as one concrete type.
/// @param snapshot The snapshot to read.
/// @param key The setting's key.
/// @return The value, or nothing when the key is absent or holds another type.
template <class T>
[[nodiscard]] std::optional<T> setting_value(const SettingsSnapshot& snapshot,
                                             std::string_view key) {
  const SettingValue* value = snapshot.find(key);
  if (value == nullptr) {
    return std::nullopt;
  }
  const T* typed = std::get_if<T>(value);
  if (typed == nullptr) {
    return std::nullopt;
  }
  return *typed;
}

/// @brief Holds the current snapshot behind a single atomic pointer.
///
/// The hot path is one atomic load: no lock, no query, no allocation. A reload builds a new snapshot
/// and swaps it, which is what lets a policy change take effect without restarting a process. The
/// version comparison makes duplicate and reordered deliveries harmless, because a listener that
/// reconnects receives the notifications it missed in whatever order the database delivers them.
class SettingsStore {
 public:
  /// @brief Starts with @p initial as the published snapshot.
  explicit SettingsStore(std::shared_ptr<const SettingsSnapshot> initial) noexcept
      : current_(std::move(initial)) {}

  /// @brief The snapshot a reader should use now.
  [[nodiscard]] std::shared_ptr<const SettingsSnapshot> load() const noexcept {
    return current_.load(std::memory_order_acquire);
  }

  /// @brief Installs @p next when it is newer than the current snapshot.
  /// @return Whether it was installed.
  bool publish(const std::shared_ptr<const SettingsSnapshot>& next) noexcept {
    if (!next) {
      return false;
    }
    std::shared_ptr<const SettingsSnapshot> current = load();
    while (current != nullptr && next->version > current->version) {
      if (current_.compare_exchange_weak(current, next, std::memory_order_release,
                                         std::memory_order_acquire)) {
        return true;
      }
    }
    return false;
  }

  /// @brief The version of the published snapshot, or zero when none is loaded.
  [[nodiscard]] std::int64_t version() const noexcept {
    const std::shared_ptr<const SettingsSnapshot> snapshot = load();
    return snapshot == nullptr ? 0 : snapshot->version;
  }

 private:
  std::atomic<std::shared_ptr<const SettingsSnapshot>> current_;
};

}  // namespace cms
