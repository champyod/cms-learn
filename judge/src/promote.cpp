#include <cms/judge/promote.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <fstream>
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
                  const std::filesystem::path& reference) -> Result<Promotion> {
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
  const auto verified_at = original->find("\"status\": \"verified\"");
  if (verified_at == std::string::npos) {
    return std::unexpected(Error::make(
        ErrorCode::invalid_argument,
        task_dir.string() + ": verification is pending, and a task is published only on machine evidence"));
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
