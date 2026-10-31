#include <cms/api/uuid.hpp>

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>

TEST_CASE("a generated uuid has the canonical shape", "[uuid]") {
  const auto value = cms::api::uuid_v4();

  CHECK(value.size() == 36);
  CHECK(value[8] == '-');
  CHECK(value[13] == '-');
  CHECK(value[18] == '-');
  CHECK(value[23] == '-');
  CHECK(value[14] == '4');
  CHECK(std::string{"89ab"}.find(value[19]) != std::string::npos);
}

TEST_CASE("an underscore is the only character outside hex and dashes", "[uuid]") {
  const auto value = cms::api::uuid_v4();
  const auto allowed = std::string{"0123456789abcdef-"};

  for (const auto character : value) {
    CHECK(allowed.find(character) != std::string::npos);
  }
}

TEST_CASE("two draws differ", "[uuid]") {
  auto seen = std::set<std::string>{};
  for (auto index = 0; index < 1000; ++index) {
    seen.insert(cms::api::uuid_v4());
  }
  CHECK(seen.size() == 1000);
}
