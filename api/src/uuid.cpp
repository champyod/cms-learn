#include <cms/api/uuid.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

#include <sodium.h>

namespace cms::api {
namespace {

constexpr std::size_t UUID_BYTES = 16;
constexpr std::size_t UUID_TEXT_LENGTH = 36;
constexpr unsigned VERSION_4 = 0x40;
constexpr unsigned VARIANT_RFC_4122 = 0x80;
constexpr unsigned LOW_FOUR_BITS = 0x0f;
constexpr unsigned LOW_SIX_BITS = 0x3f;
constexpr unsigned NIBBLE_SHIFT = 4;

constexpr std::array<char, 16> HEX_DIGITS{'0', '1', '2', '3', '4', '5', '6', '7',
                                          '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

/// libsodium's initialiser is idempotent and thread-safe, but it must run before any randomness.
auto ensure_sodium_ready() -> void {
  static const auto ready = [] {
    if (sodium_init() < 0) {
      throw std::runtime_error("libsodium could not initialise");
    }
    return true;
  }();
  (void)ready;
}

}  // namespace

auto uuid_v4() -> std::string {
  ensure_sodium_ready();

  auto bytes = std::array<unsigned char, UUID_BYTES>{};
  randombytes_buf(bytes.data(), bytes.size());
  const auto as_unsigned = [](unsigned char value) { return static_cast<unsigned>(value); };
  bytes[6] = static_cast<unsigned char>((as_unsigned(bytes[6]) & LOW_FOUR_BITS) | VERSION_4);
  bytes[8] = static_cast<unsigned char>((as_unsigned(bytes[8]) & LOW_SIX_BITS) | VARIANT_RFC_4122);

  auto text = std::string{};
  text.reserve(UUID_TEXT_LENGTH);
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    if (index == 4 || index == 6 || index == 8 || index == 10) {
      text.push_back('-');
    }
    text.push_back(HEX_DIGITS[as_unsigned(bytes[index]) >> NIBBLE_SHIFT]);
    text.push_back(HEX_DIGITS[as_unsigned(bytes[index]) & LOW_FOUR_BITS]);
  }
  return text;
}

}  // namespace cms::api
