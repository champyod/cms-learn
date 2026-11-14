#include <cms/object_store.hpp>

#include <aws/core/Aws.h>
#include <aws/core/auth/AWSCredentials.h>
#include <aws/core/client/ClientConfiguration.h>
#include <aws/core/utils/memory/stl/AWSStringStream.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/S3ClientConfiguration.h>
#include <aws/s3/S3EndpointProvider.h>
#include <aws/s3/S3Errors.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/GetObjectResult.h>
#include <aws/s3/model/PutObjectRequest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <istream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

#include <sodium.h>

namespace cms::store {
namespace {

constexpr char ALLOCATION_TAG[] = "cms::store";
constexpr std::size_t DIGEST_LENGTH = 64;
constexpr std::size_t HEX_DIGITS_PER_BYTE = 2;
constexpr unsigned NIBBLE_SHIFT = 4;
constexpr unsigned NIBBLE_MASK = 0x0fu;

/// The SDK starts its background runtime once per process; logging stays off so no request detail reaches a
/// log line.
auto sdk_runtime() -> bool {
  static const bool initialized = [] {
    Aws::SDKOptions options;
    options.loggingOptions.logLevel = Aws::Utils::Logging::LogLevel::Off;
    Aws::InitAPI(options);
    return true;
  }();
  return initialized;
}

auto has_scheme(std::string_view endpoint, std::string_view scheme) -> bool {
  return endpoint.rfind(scheme, 0) == 0;
}

class S3Store final : public ObjectStore {
 public:
  S3Store(const StoreConfig& config, const Aws::S3::S3ClientConfiguration& client)
      : bucket_(config.bucket),
        client_(Aws::Auth::AWSCredentials{config.access_key_id, config.secret_access_key},
                Aws::MakeShared<Aws::S3::S3EndpointProvider>(ALLOCATION_TAG), client) {}

  auto read(std::string_view key) const -> Result<std::string> override { return fetch(key); }

  auto read_verified(std::string_view key, std::string_view digest) const -> Result<std::string> override {
    if (!is_digest(digest)) {
      return std::unexpected(Error::make(
          ErrorCode::invalid_argument,
          "the row records no content address for the object at key " + std::string{key}));
    }
    const auto bytes = fetch(key);
    if (!bytes) {
      return std::unexpected(bytes.error());
    }
    const auto stored = digest_of(*bytes);
    if (stored != digest) {
      return std::unexpected(Error::make(
          ErrorCode::conflict, "the object at key " + std::string{key} +
                                   " does not match the digest its row records: the row says " +
                                   std::string{digest} + ", the stored bytes hash to " + stored));
    }
    return bytes;
  }

  auto write(std::string_view key, std::string_view bytes) -> Result<std::string> override {
    auto body = Aws::MakeShared<Aws::StringStream>(ALLOCATION_TAG);
    body->write(bytes.data(), static_cast<std::streamsize>(bytes.size()));

    auto request = Aws::S3::Model::PutObjectRequest{};
    request.SetBucket(bucket_);
    request.SetKey(std::string{key});
    request.SetBody(body);

    const auto outcome = client_.PutObject(request);
    if (!outcome.IsSuccess()) {
      return std::unexpected(Error::make(ErrorCode::unavailable, "the store refused " + std::string{key} +
                                                                     ": " +
                                                                     outcome.GetError().GetMessage()));
    }
    return digest_of(bytes);
  }

 private:
  auto fetch(std::string_view key) const -> Result<std::string> {
    auto request = Aws::S3::Model::GetObjectRequest{};
    request.SetBucket(bucket_);
    request.SetKey(std::string{key});

    auto outcome = client_.GetObject(request);
    if (outcome.IsSuccess()) {
      auto& body = outcome.GetResult().GetBody();
      auto bytes = std::string{};
      for (std::istreambuf_iterator<char> it{body}; it != std::istreambuf_iterator<char>{}; ++it) {
        bytes.push_back(*it);
      }
      return bytes;
    }
    const auto& failure = outcome.GetError();
    if (failure.GetResponseCode() == Aws::Http::HttpResponseCode::NOT_FOUND ||
        failure.GetErrorType() == Aws::S3::S3Errors::NO_SUCH_KEY) {
      return std::unexpected(
          Error::make(ErrorCode::not_found, "no object at key " + std::string{key}));
    }
    return std::unexpected(Error::make(ErrorCode::unavailable, "the store refused " +
                                                                   std::string{key} + ": " +
                                                                   failure.GetMessage()));
  }

  std::string bucket_;
  Aws::S3::S3Client client_;
};

}  // namespace

auto digest_of(std::string_view bytes) -> std::string {
  auto hash = std::array<unsigned char, crypto_hash_sha256_BYTES>{};
  crypto_hash_sha256(hash.data(), reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size());
  constexpr std::string_view DIGITS = "0123456789abcdef";
  auto hex = std::string{};
  hex.reserve(hash.size() * HEX_DIGITS_PER_BYTE);
  for (const auto byte : hash) {
    hex.push_back(DIGITS[byte >> NIBBLE_SHIFT]);
    hex.push_back(DIGITS[byte & NIBBLE_MASK]);
  }
  return hex;
}

auto is_digest(std::string_view digest) -> bool {
  if (digest.size() != DIGEST_LENGTH) {
    return false;
  }
  for (const auto character : digest) {
    const auto is_digit = character >= '0' && character <= '9';
    const auto is_lower_hex = character >= 'a' && character <= 'f';
    if (!is_digit && !is_lower_hex) {
      return false;
    }
  }
  return true;
}

auto make_store(const StoreConfig& config) -> Result<std::unique_ptr<ObjectStore>> {
  auto missing = std::string{};
  const auto require = [&missing](const std::string& value, std::string_view name) {
    if (value.empty()) {
      if (!missing.empty()) {
        missing += ", ";
      }
      missing += name;
    }
  };
  require(config.endpoint, "endpoint");
  require(config.region, "region");
  require(config.bucket, "bucket");
  require(config.access_key_id, "access key id");
  require(config.secret_access_key, "secret access key");
  if (!missing.empty()) {
    return std::unexpected(
        Error::make(ErrorCode::invalid_argument, "the object store is missing " + missing));
  }

  try {
    sdk_runtime();
    auto client = Aws::S3::S3ClientConfiguration{};
    client.region = config.region;
    client.endpointOverride = config.endpoint;
    client.scheme =
        has_scheme(config.endpoint, "http://") ? Aws::Http::Scheme::HTTP : Aws::Http::Scheme::HTTPS;
    // Path-style addressing: the bucket is named by the deployment and reached by its endpoint, so it must
    // not be moved into the hostname. The SDK keeps the flag on the endpoint provider, not on the config.
    client.useVirtualAddressing = false;
    return std::unique_ptr<ObjectStore>{new S3Store(config, client)};
  } catch (const std::exception& failure) {
    return std::unexpected(Error::make(ErrorCode::unavailable, failure.what()));
  }
}

}  // namespace cms::store
