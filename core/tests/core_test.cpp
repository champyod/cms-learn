#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "cms/capability.hpp"
#include "cms/settings_snapshot.hpp"

namespace {

std::shared_ptr<const cms::SettingsSnapshot> snapshot_with_version(std::int64_t version) {
  auto snapshot = std::make_shared<cms::SettingsSnapshot>();
  snapshot->version = version;
  return snapshot;
}

}  // namespace

TEST_CASE("every capability round-trips through its name", "[capability]") {
  for (const auto& entry : cms::k_capability_names) {
    const auto parsed = cms::capability_from_name(entry.name);
    REQUIRE(parsed.has_value());
    REQUIRE(*parsed == entry.capability);
    REQUIRE(cms::capability_name(entry.capability) == entry.name);
  }
}

TEST_CASE("a capability name is unique and never a wildcard", "[capability]") {
  for (const auto& entry : cms::k_capability_names) {
    REQUIRE(entry.name.find('*') == std::string_view::npos);
    REQUIRE(entry.name.find(':') != std::string_view::npos);
    std::size_t occurrences = 0;
    for (const auto& other : cms::k_capability_names) {
      if (other.name == entry.name) {
        ++occurrences;
      }
    }
    REQUIRE(occurrences == 1);
  }
}

TEST_CASE("the system principal holds exactly the enumerated set", "[capability]") {
  const cms::CapabilitySet root = cms::all_capabilities();
  for (const auto& entry : cms::k_capability_names) {
    REQUIRE(root.has(entry.capability));
  }
  const std::uint64_t expected_mask = (std::uint64_t{1} << cms::k_capability_count) - 1;
  REQUIRE(root.bits() == expected_mask);
  REQUIRE(cms::capability_set_from_bits(root.bits()) == root);
}

TEST_CASE("grant and revoke touch one bit", "[capability]") {
  cms::CapabilitySet set;
  REQUIRE_FALSE(set.has(cms::Capability::settings_write));

  set.grant(cms::Capability::settings_write);
  REQUIRE(set.has(cms::Capability::settings_write));
  REQUIRE(set.bits() == (std::uint64_t{1} << 9));

  set.revoke(cms::Capability::settings_write);
  REQUIRE_FALSE(set.has(cms::Capability::settings_write));
  REQUIRE(set.bits() == 0);
}

TEST_CASE("a stale or repeated snapshot is refused", "[settings]") {
  cms::SettingsStore store(snapshot_with_version(7));

  REQUIRE(store.version() == 7);
  REQUIRE_FALSE(store.publish(snapshot_with_version(7)));
  REQUIRE_FALSE(store.publish(snapshot_with_version(3)));
  REQUIRE_FALSE(store.publish(nullptr));
  REQUIRE(store.version() == 7);

  REQUIRE(store.publish(snapshot_with_version(8)));
  REQUIRE(store.version() == 8);
}

TEST_CASE("a setting is read as its declared type only", "[settings]") {
  auto snapshot = std::make_shared<cms::SettingsSnapshot>();
  snapshot->version = 1;
  snapshot->values.emplace("registration_open", true);
  snapshot->values.emplace("session_ttl_minutes", std::int64_t{90});
  snapshot->values.emplace("site_name", std::string("Learn"));

  REQUIRE(cms::setting_value<bool>(*snapshot, "registration_open").value_or(false));
  REQUIRE(cms::setting_value<std::int64_t>(*snapshot, "session_ttl_minutes").value_or(0) == 90);
  REQUIRE(cms::setting_value<std::string>(*snapshot, "site_name").value_or("") == "Learn");

  REQUIRE_FALSE(cms::setting_value<bool>(*snapshot, "session_ttl_minutes").has_value());
  REQUIRE_FALSE(cms::setting_value<std::int64_t>(*snapshot, "absent").has_value());
}
