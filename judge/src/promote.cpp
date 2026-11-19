#include <cms/judge/promote.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include <sodium.h>

#include <cms/judge/manifest.hpp>
#include <cms/judge/verify.hpp>

namespace cms::judge {
namespace {

constexpr std::size_t HEX_DIGITS_PER_BYTE = 2;
constexpr unsigned NIBBLE_SHIFT = 4;
constexpr unsigned NIBBLE_MASK = 0x0fu;

auto digest(std::string_view text) -> std::string {
  auto bytes = std::array<unsigned char, crypto_hash_sha256_BYTES>{};
  crypto_hash_sha256(bytes.data(), reinterpret_cast<const unsigned char*>(text.data()), text.size());
  constexpr std::string_view DIGITS = "0123456789abcdef";
  auto hex = std::string{};
  hex.reserve(bytes.size() * HEX_DIGITS_PER_BYTE);
  for (const auto byte : bytes) {
    hex.push_back(DIGITS[byte >> NIBBLE_SHIFT]);
    hex.push_back(DIGITS[byte & NIBBLE_MASK]);
  }
  return hex;
}

auto read_text(const std::filesystem::path& path) -> Result<std::string> {
  auto stream = std::ifstream{path, std::ios::binary};
  if (!stream) {
    return std::unexpected(Error::make(ErrorCode::not_found, "cannot read " + path.string()));
  }
  auto buffer = std::ostringstream{};
  buffer << stream.rdbuf();
  return buffer.str();
}

auto field_value(std::string_view manifest, std::string_view field) -> std::string {
  const auto key = "\"" + std::string{field} + "\"";
  const auto position = manifest.find(key);
  if (position == std::string_view::npos) {
    return {};
  }
  const auto colon = manifest.find(':', position);
  if (colon == std::string_view::npos) {
    return {};
  }
  const auto open = manifest.find('"', colon);
  if (open == std::string_view::npos) {
    return {};
  }
  const auto close = manifest.find('"', open + 1);
  if (close == std::string_view::npos) {
    return {};
  }
  return std::string{manifest.substr(open + 1, close - open - 1)};
}

auto field_integer(std::string_view manifest, std::string_view field) -> std::optional<std::int64_t> {
  const auto key = "\"" + std::string{field} + "\"";
  const auto position = manifest.find(key);
  if (position == std::string_view::npos) {
    return std::nullopt;
  }
  const auto colon = manifest.find(':', position);
  if (colon == std::string_view::npos) {
    return std::nullopt;
  }
  auto first = colon + 1;
  while (first < manifest.size() && (manifest[first] == ' ' || manifest[first] == '\t')) {
    ++first;
  }
  auto last = first;
  while (last < manifest.size() && manifest[last] >= '0' && manifest[last] <= '9') {
    ++last;
  }
  if (last == first) {
    return std::nullopt;
  }
  return std::stoll(std::string{manifest.substr(first, last - first)});
}

/// The identity a certificate must name. The file carries no verification claim to compare against (ADR 0046 §2): the
/// certificate row is the only thing that says a task is verified, and a refusal names that row.
struct ManifestIdentity {
  std::string task_id;
  std::int64_t task_version{0};
};

auto manifest_identity(std::string_view manifest, const std::filesystem::path& task_dir)
    -> Result<ManifestIdentity> {
  const auto task_id = field_value(manifest, "id");
  const auto task_version = field_integer(manifest, "version");
  if (task_id.empty() || !task_version) {
    return std::unexpected(Error::make(
        ErrorCode::invalid_argument,
        task_dir.string() + ": the manifest names no id or version, so no certificate can be read for it"));
  }
  return ManifestIdentity{.task_id = task_id, .task_version = *task_version};
}

auto refused(const std::filesystem::path& task_dir, std::string_view reason) -> Error {
  return Error::make(ErrorCode::invalid_argument,
                     task_dir.string() + ": " + std::string{reason} +
                         "; a task is published only on a certificate the certifier wrote that says verified");
}

/// @brief Refuses unless the newest certificate for the manifest's task version says `verified`.
auto refuse_unless_certified(const std::filesystem::path& task_dir, const ManifestIdentity& identity,
                             const CertificateSource& certificates) -> Result<void> {
  const auto certificate = certificates.latest(identity.task_id, identity.task_version);
  if (!certificate) {
    return std::unexpected(certificate.error());
  }
  if (!certificate->has_value()) {
    return std::unexpected(refused(task_dir, "no certificate row exists for " + identity.task_id + " version " +
                                               std::to_string(identity.task_version)));
  }
  const auto& row = **certificate;
  if (row.outcome != "verified") {
    return std::unexpected(refused(task_dir, "the certificate row records \"" + row.outcome + "\" (commit " +
                                               row.commit_id + ")"));
  }
  return {};
}

/// Replaces one occurrence and refuses when the text is not there, so a promotion cannot quietly write a
/// manifest it did not understand.
auto replace_once(std::string& text, std::string_view from, std::string_view to) -> bool {
  const auto position = text.find(from);
  if (position == std::string::npos) {
    return false;
  }
  text.replace(position, from.size(), to);
  return true;
}

}  // namespace

auto promote_task(const std::filesystem::path& task_dir, const std::filesystem::path& artifact_dir,
                  const std::filesystem::path& reference, const CertificateSource& certificates)
    -> Result<Promotion> {
  const auto manifest_path = task_dir / "task.json";
  const auto original = read_text(manifest_path);
  if (!original) {
    return std::unexpected(original.error());
  }

  // Cheap refusals first: a missing machine verdict is not the same failure as a differing byte, and the
  // operator needs to know which one stopped the publish.
  if (field_value(*original, "status") == "published") {
    return std::unexpected(
        Error::make(ErrorCode::conflict, task_dir.string() + " is already published"));
  }
  const auto identity = manifest_identity(*original, task_dir);
  if (!identity) {
    return std::unexpected(identity.error());
  }
  if (const auto certified = refuse_unless_certified(task_dir, *identity, certificates); !certified) {
    return std::unexpected(certified.error());
  }

  const auto verified = verify_task(task_dir, reference);
  if (!verified) {
    return std::unexpected(verified.error());
  }

  auto published = *original;
  if (!replace_once(published, "\"status\": \"draft\"", "\"status\": \"published\"") &&
      !replace_once(published, "\"status\": \"verified\"", "\"status\": \"published\"")) {
    return std::unexpected(
        Error::make(ErrorCode::invalid_argument, task_dir.string() + ": the manifest has no draft status"));
  }

  const auto artifact_digest = digest(published);
  std::error_code ignored;
  std::filesystem::create_directories(artifact_dir, ignored);
  const auto artifact = artifact_dir / (artifact_digest + ".json");

  auto stream = std::ofstream{artifact, std::ios::trunc};
  if (!stream) {
    return std::unexpected(
        Error::make(ErrorCode::unavailable, "cannot write the artifact at " + artifact.string()));
  }
  stream << published;
  stream.close();

  auto manifest_stream = std::ofstream{manifest_path, std::ios::trunc};
  if (!manifest_stream) {
    return std::unexpected(
        Error::make(ErrorCode::unavailable, "cannot write " + manifest_path.string()));
  }
  manifest_stream << published;
  return Promotion{.digest = artifact_digest, .artifact = artifact};
}

}  // namespace cms::judge
