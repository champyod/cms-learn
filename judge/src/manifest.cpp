#include <cms/judge/manifest.hpp>

#include <array>
#include <cstddef>
#include <fstream>
#include <string>
#include <string_view>

namespace cms::judge {
namespace {

constexpr std::size_t INDENT = 2;

/// Only what a manifest can hold reaches this: relative paths, task ids and hex digests. The escape is
/// here anyway, because a manifest written by a future task id must not be able to break the document.
auto json_text(std::string_view text) -> std::string {
  auto escaped = std::string{};
  escaped.reserve(text.size() + 2);
  escaped.push_back('"');
  for (const auto character : text) {
    if (character == '"' || character == '\\') {
      escaped.push_back('\\');
    }
    escaped.push_back(character);
  }
  escaped.push_back('"');
  return escaped;
}

auto indent(std::size_t depth) -> std::string { return std::string(depth * INDENT, ' '); }

}  // namespace

auto render_manifest(const Manifest& manifest) -> std::string {
  auto text = std::string{};
  text += "{\n";
  text += indent(1) + "\"task\": " + json_text(manifest.task) + ",\n";
  text += indent(1) + "\"reference_digest\": " + json_text(manifest.reference_digest) + ",\n";
  text += indent(1) + "\"cases\": [\n";
  for (std::size_t index = 0; index < manifest.cases.size(); ++index) {
    const auto& entry = manifest.cases[index];
    text += indent(2) + "{\n";
    text += indent(3) + "\"input\": " + json_text(entry.input) + ",\n";
    text += indent(3) + "\"output\": " + json_text(entry.output) + ",\n";
    text += indent(3) + "\"input_sha256\": " + json_text(entry.input_digest) + ",\n";
    text += indent(3) + "\"output_sha256\": " + json_text(entry.output_digest) + "\n";
    text += indent(2) + "}";
    text += index + 1 == manifest.cases.size() ? "\n" : ",\n";
  }
  text += indent(1) + "]\n";
  text += "}\n";
  return text;
}

auto write_manifest(const std::filesystem::path& path, const Manifest& manifest) -> Result<void> {
  std::error_code ignored;
  std::filesystem::create_directories(path.parent_path(), ignored);
  auto stream = std::ofstream{path, std::ios::trunc};
  if (!stream) {
    return std::unexpected(
        Error::make(ErrorCode::unavailable, "cannot write " + path.string()));
  }
  stream << render_manifest(manifest);
  if (!stream) {
    return std::unexpected(
        Error::make(ErrorCode::unavailable, "cannot write " + path.string()));
  }
  return {};
}

}  // namespace cms::judge
