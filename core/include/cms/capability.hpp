#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace cms {

/// One authority the core can exercise.
///
/// The enumeration is closed on purpose. A role is a row, so an operator can be granted a capability
/// that exists here and can never create one that does not; the mask stays diffable, and no wildcard
/// is representable. A mask of zero is a valid, auditable principal: it can read nothing.
enum class Capability : std::uint8_t {
  content_read,
  content_write,
  content_publish,
  submission_read,
  submission_grade,
  user_read,
  user_write,
  user_approve,
  settings_read,
  settings_write,
  infra_read,
  infra_write,
  audit_read,
  schema_apply,
};

inline constexpr std::size_t k_capability_count = 14;
static_assert(k_capability_count <= 64U, "a std::uint64_t mask cannot hold this many capabilities");

struct CapabilityName {
  Capability capability;
  std::string_view name;

  friend constexpr bool operator==(const CapabilityName&, const CapabilityName&) = default;
};

/// The spelling every other surface uses for a capability: the database, the control socket and the
/// audit log all carry this string. Order is asserted in the core tests, so a name can never point at
/// the wrong bit after an entry is inserted.
inline constexpr std::array<CapabilityName, k_capability_count> k_capability_names{{
    {Capability::content_read, "content:read"},
    {Capability::content_write, "content:write"},
    {Capability::content_publish, "content:publish"},
    {Capability::submission_read, "submission:read"},
    {Capability::submission_grade, "submission:grade"},
    {Capability::user_read, "user:read"},
    {Capability::user_write, "user:write"},
    {Capability::user_approve, "user:approve"},
    {Capability::settings_read, "settings:read"},
    {Capability::settings_write, "settings:write"},
    {Capability::infra_read, "infra:read"},
    {Capability::infra_write, "infra:write"},
    {Capability::audit_read, "audit:read"},
    {Capability::schema_apply, "schema:apply"},
}};

/// The registered name of a capability, or an empty view when the value did not come from the
/// enumeration. Callers treat empty as "deny": an unrecognised value must never widen authority.
[[nodiscard]] constexpr std::string_view capability_name(Capability capability) noexcept {
  for (const auto& entry : k_capability_names) {
    if (entry.capability == capability) {
      return entry.name;
    }
  }
  return {};
}

/// The inverse of capability_name, used when a role row or a control-socket request is read.
[[nodiscard]] constexpr std::optional<Capability> capability_from_name(
    std::string_view name) noexcept {
  for (const auto& entry : k_capability_names) {
    if (entry.name == name) {
      return entry.capability;
    }
  }
  return std::nullopt;
}

/// A set of capabilities. Every check is one AND against a value the request already holds, so an
/// authorisation decision never costs a query and never branches on a role name.
class CapabilitySet {
 public:
  constexpr CapabilitySet() noexcept = default;

  [[nodiscard]] constexpr bool has(Capability capability) const noexcept {
    return (bits_ & bit_of(capability)) != 0;
  }

  constexpr void grant(Capability capability) noexcept { bits_ |= bit_of(capability); }
  constexpr void revoke(Capability capability) noexcept { bits_ &= ~bit_of(capability); }

  [[nodiscard]] constexpr std::uint64_t bits() const noexcept { return bits_; }

  friend constexpr bool operator==(const CapabilitySet&, const CapabilitySet&) noexcept = default;

 private:
  static constexpr std::uint64_t bit_of(Capability capability) noexcept {
    return std::uint64_t{1} << static_cast<std::uint64_t>(capability);
  }

  std::uint64_t bits_{0};
};

[[nodiscard]] constexpr CapabilitySet capability_set_from_bits(std::uint64_t bits) noexcept {
  CapabilitySet set;
  for (const auto& entry : k_capability_names) {
    if ((bits & (std::uint64_t{1} << static_cast<std::uint64_t>(entry.capability))) != 0) {
      set.grant(entry.capability);
    }
  }
  return set;
}

/// Every capability that exists. The system principal holds exactly this set and nothing more, so
/// "root can do anything" is answerable by reading the enumeration rather than by trusting a rule.
[[nodiscard]] constexpr CapabilitySet all_capabilities() noexcept {
  CapabilitySet set;
  for (const auto& entry : k_capability_names) {
    set.grant(entry.capability);
  }
  return set;
}

}  // namespace cms
