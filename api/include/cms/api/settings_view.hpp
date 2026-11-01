#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <cms/error.hpp>
#include <cms/settings_snapshot.hpp>

namespace cms::api {

/// @brief One setting as the admin surface may show it.
struct VisibleSetting {
  std::string key;
  std::string kind;
  std::string tier;
  /// The stored value, or the redaction marker when the tier is secret. Defaulted so a caller can build the
  /// first three fields at once and fill the value afterwards.
  std::string value{};
};

/// @brief The catalogue: every key the build knows, with its kind, tier and reload class.
struct CatalogueEntry {
  std::string key;
  std::string kind;
  std::string tier;
  std::string fallback;
  /// `hot` or `restart`: whether a new value reaches the running service without being restarted.
  std::string reload;
};

/// @brief The redaction marker a secret is replaced with on every read.
///
/// A constant rather than a literal at each call site: the marker is part of the contract the UI and any
/// script consuming this list agree on.
inline constexpr std::string_view REDACTED = "<redacted>";

/// @brief Reads the generated settings catalogue.
[[nodiscard]] Result<std::vector<CatalogueEntry>> load_catalogue(const std::filesystem::path& path);

/// @brief The settings an admin surface may show, with every secret redacted.
///
/// @param catalogue The keys the build knows.
/// @param snapshot The stored values.
/// @return One entry per catalogued key, never carrying a secret's value.
///
/// A key in the snapshot that is not in the catalogue is omitted rather than shown: the catalogue is what
/// this build understands, and rendering an unknown key would offer a setting nothing honours.
[[nodiscard]] std::vector<VisibleSetting> visible_settings(const std::vector<CatalogueEntry>& catalogue,
                                                           const SettingsSnapshot& snapshot);

/// @brief Whether @p key may be written from an admin surface.
///
/// A secret may be written — that is how a credential is rotated — but it may never be read back, which is
/// what visible_settings enforces.
[[nodiscard]] bool is_catalogued(const std::vector<CatalogueEntry>& catalogue, std::string_view key);

/// @brief The visible list for one snapshot version, built once and then handed out.
///
/// The catalogue is a generated file and a snapshot is immutable, so the list is a pure function of the two.
/// A surface that renders it per request would rebuild every entry and copy three strings each time for an
/// answer that has not changed since the last version; this keeps the last list and shares it.
class SettingsView {
 public:
  /// @brief Takes the catalogue this view answers from.
  explicit SettingsView(std::vector<CatalogueEntry> catalogue);

  /// @brief The list for @p snapshot, reusing the previous one while its version is unchanged.
  ///
  /// The returned view stays valid as long as the caller holds it, including after a later version replaces
  /// it: a request rendering the previous list is not invalidated by the next reload.
  [[nodiscard]] auto for_snapshot(const SettingsSnapshot& snapshot) const
      -> std::shared_ptr<const std::vector<VisibleSetting>>;

 private:
  std::vector<CatalogueEntry> catalogue_;
  mutable std::mutex mutex_;
  mutable std::int64_t version_{-1};
  mutable std::shared_ptr<const std::vector<VisibleSetting>> built_;
};

}  // namespace cms::api
