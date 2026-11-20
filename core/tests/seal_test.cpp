#include <cms/seal.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <string_view>

namespace {

constexpr auto KEY = std::string_view{"0123456789abcdef0123456789abcdef"};
constexpr auto OTHER_KEY = std::string_view{"fedcba9876543210fedcba9876543210"};

/// @return @p sealed with one of its ciphertext bytes flipped, so the box no longer opens.
auto with_flipped_byte(std::string sealed) -> std::string {
  const auto last = sealed.size() - 1;
  sealed[last] = sealed[last] == 'A' ? 'B' : 'A';
  return sealed;
}

}  // namespace

TEST_CASE("a sealed value unseals to the bytes it was sealed from", "[seal]") {
  const auto sealer = cms::seal::Sealer::from_key(KEY);
  REQUIRE(sealer.has_value());

  const auto sealed = sealer->seal("the deployment's secret");
  REQUIRE(sealed.has_value());
  CHECK(cms::seal::is_sealed(*sealed));

  const auto opened = sealer->unseal(*sealed);
  REQUIRE(opened.has_value());
  CHECK(*opened == "the deployment's secret");
}

TEST_CASE("text that is not ASCII survives the round trip byte for byte", "[seal]") {
  const auto sealer = cms::seal::Sealer::from_key(KEY);
  REQUIRE(sealer.has_value());
  const auto secret = std::string{"รหัสผ่านของ deployment — 32 bytes of Thai and dashes"};

  const auto sealed = sealer->seal(secret);
  REQUIRE(sealed.has_value());
  const auto opened = sealer->unseal(*sealed);
  REQUIRE(opened.has_value());
  CHECK(*opened == secret);
}

TEST_CASE("the nonce travels with the ciphertext and is never reused", "[seal]") {
  const auto sealer = cms::seal::Sealer::from_key(KEY);
  REQUIRE(sealer.has_value());

  const auto first = sealer->seal("same secret");
  const auto second = sealer->seal("same secret");
  REQUIRE(first.has_value());
  REQUIRE(second.has_value());
  // Two seals of one value differ, because the nonce is random per call; a reused nonce is the defect this prevents.
  CHECK(*first != *second);
  const auto opened_first = sealer->unseal(*first);
  const auto opened_second = sealer->unseal(*second);
  REQUIRE(opened_first.has_value());
  REQUIRE(opened_second.has_value());
  CHECK(*opened_first == "same secret");
  CHECK(*opened_second == "same secret");
  // One string carries everything the reader needs: the version, the nonce and the box.
  CHECK(first->starts_with(cms::seal::PREFIX));
  CHECK(first->find(':') != first->rfind(':'));
}

TEST_CASE("another deployment's key cannot open the value", "[seal]") {
  const auto sealer = cms::seal::Sealer::from_key(KEY);
  const auto stranger = cms::seal::Sealer::from_key(OTHER_KEY);
  REQUIRE(sealer.has_value());
  REQUIRE(stranger.has_value());
  const auto sealed = sealer->seal("the deployment's secret");
  REQUIRE(sealed.has_value());

  const auto opened = stranger->unseal(*sealed);
  REQUIRE(!opened.has_value());
  CHECK(opened.error().message.find("cannot be opened with this deployment's key") != std::string::npos);
  // Neither the value nor either key appears anywhere in the refusal.
  CHECK(opened.error().message.find("secret") == std::string::npos);
  CHECK(opened.error().message.find(KEY) == std::string::npos);
  CHECK(opened.error().message.find(OTHER_KEY) == std::string::npos);
}

TEST_CASE("a truncated value and a flipped byte are both refused by name", "[seal]") {
  const auto sealer = cms::seal::Sealer::from_key(KEY);
  REQUIRE(sealer.has_value());
  const auto sealed = sealer->seal("the deployment's secret");
  REQUIRE(sealed.has_value());

  // A box cut short and a box from another key are the same failure to the authenticator, and both are refused with the
  // sentence that names it.
  const auto truncated = sealer->unseal(sealed->substr(0, sealed->size() - 8));
  REQUIRE(!truncated.has_value());
  CHECK(truncated.error().message.find("cannot be opened") != std::string::npos);

  // A value whose shape is wrong — no nonce, no box, not base64 — is refused before any key is tried.
  const auto shapeless = sealer->unseal(std::string{cms::seal::PREFIX} + "not-base64");
  REQUIRE(!shapeless.has_value());
  CHECK(shapeless.error().message.find("malformed") != std::string::npos);

  const auto flipped = sealer->unseal(with_flipped_byte(*sealed));
  REQUIRE(!flipped.has_value());
  CHECK(flipped.error().message.find("cannot be opened") != std::string::npos);
  CHECK(flipped.error().message.find("secret") == std::string::npos);
}

TEST_CASE("a value that was never sealed is refused rather than read as plaintext", "[seal]") {
  const auto sealer = cms::seal::Sealer::from_key(KEY);
  REQUIRE(sealer.has_value());

  CHECK_FALSE(cms::seal::is_sealed("a plaintext password"));
  const auto opened = sealer->unseal("a plaintext password");
  REQUIRE(!opened.has_value());
  CHECK(opened.error().message.find("not a sealed secret") != std::string::npos);
}

TEST_CASE("a key of the wrong length is refused and its bytes are not printed", "[seal]") {
  const auto short_key = cms::seal::Sealer::from_key("too-short");
  REQUIRE(!short_key.has_value());
  CHECK(short_key.error().message.find("exactly 32 bytes") != std::string::npos);
  CHECK(short_key.error().message.find("too-short") == std::string::npos);
}

TEST_CASE("the process sealer is configured once and never replaced by another key", "[seal]") {
  REQUIRE(cms::seal::configure(KEY).has_value());
  const auto configured = cms::seal::sealer();
  REQUIRE(configured.has_value());
  const auto sealed = configured->seal("a secret");
  REQUIRE(sealed.has_value());
  const auto opened = cms::seal::sealer()->unseal(*sealed);
  REQUIRE(opened.has_value());
  CHECK(*opened == "a secret");

  // The same key again is accepted; a different one is refused rather than replacing what rows were sealed with.
  CHECK(cms::seal::configure(KEY).has_value());
  const auto replaced = cms::seal::configure(OTHER_KEY);
  REQUIRE(!replaced.has_value());
  CHECK(replaced.error().message.find("changed after it was configured") != std::string::npos);
}
