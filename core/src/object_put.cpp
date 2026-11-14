#include <cms/object_store.hpp>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

auto from_environment(std::string_view name) -> std::string {
  const auto* value = std::getenv(std::string{name}.c_str());
  return value == nullptr ? std::string{} : std::string{value};
}

auto read_file(const char* path) -> cms::Result<std::string> {
  auto stream = std::ifstream{path, std::ios::binary};
  if (!stream) {
    return std::unexpected(
        cms::Error::make(cms::ErrorCode::not_found, std::string{"cannot read "} + path));
  }
  auto buffer = std::ostringstream{};
  buffer << stream.rdbuf();
  return buffer.str();
}

}  // namespace

/// Writes one object and prints the digest the caller must record; a refusal names its reason and no value.
auto main(int argc, char** argv) -> int {
  if (argc != 3) {
    std::cerr << "usage: cms_object_put <key> <file>\n";
    return 64;
  }
  const auto bytes = read_file(argv[2]);
  if (!bytes) {
    std::cerr << bytes.error().message << "\n";
    return 2;
  }

  const auto config = cms::store::StoreConfig{
      .endpoint = from_environment("CMSLEARN_S3_ENDPOINT"),
      .region = from_environment("CMSLEARN_S3_REGION"),
      .bucket = from_environment("CMSLEARN_S3_BUCKET"),
      .access_key_id = from_environment("CMSLEARN_S3_ACCESS_KEY_ID"),
      .secret_access_key = from_environment("CMSLEARN_S3_SECRET_ACCESS_KEY")};

  const auto store = cms::store::make_store(config);
  if (!store) {
    std::cerr << store.error().message << "\n";
    return 2;
  }
  const auto digest = (*store)->write(argv[1], *bytes);
  if (!digest) {
    std::cerr << digest.error().message << "\n";
    return digest.error().code == cms::ErrorCode::not_found ? 4 : 1;
  }
  std::cout << *digest << "\n";
  return 0;
}
