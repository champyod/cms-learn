#include <cms/object_store.hpp>

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {

auto from_environment(std::string_view name) -> std::string {
  const auto* value = std::getenv(std::string{name}.c_str());
  return value == nullptr ? std::string{} : std::string{value};
}

}  // namespace

/// Reads one object and prints its bytes; every refusal names its reason and never the credential.
///
/// A second argument is the digest the row records, and then the bytes are checked against it before they
/// are printed: that is the read an artefact goes through.
auto main(int argc, char** argv) -> int {
  if (argc != 2 && argc != 3) {
    std::cerr << "usage: cms_object_get <key> [<digest>]\n";
    return 64;
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
  const auto bytes = argc == 3 ? (*store)->read_verified(argv[1], argv[2]) : (*store)->read(argv[1]);
  if (!bytes) {
    std::cerr << bytes.error().message << "\n";
    return bytes.error().code == cms::ErrorCode::not_found ? 4 : 1;
  }
  std::cout << *bytes;
  return 0;
}
