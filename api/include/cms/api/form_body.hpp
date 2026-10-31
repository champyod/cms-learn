#pragma once

#include <cctype>
#include <string>
#include <string_view>

namespace cms::api {

/// @brief The value of one field in an `application/x-www-form-urlencoded` body.
///
/// @param body The raw request body.
/// @param name The field to read.
/// @return The decoded value, or an empty string when the field is absent.
///
/// Only what a form actually sends is decoded: `+` is a space and `%XX` is one byte. A malformed escape is
/// left as written rather than dropped, so a corrupted submission is stored as it arrived and the judge
/// reports a compile error instead of the service silently altering someone's program.
[[nodiscard]] inline auto form_field(std::string_view body, std::string_view name) -> std::string {
  const auto key = std::string{name} + "=";
  auto position = body.find(key);
  if (position == std::string_view::npos) {
    return {};
  }
  const auto start = position + key.size();
  const auto end = body.find('&', start);
  const auto raw = body.substr(start, end == std::string_view::npos ? end : end - start);

  auto decoded = std::string{};
  decoded.reserve(raw.size());
  for (auto index = std::size_t{0}; index < raw.size(); ++index) {
    if (raw[index] == '+') {
      decoded.push_back(' ');
      continue;
    }
    const auto is_escape = raw[index] == '%' && index + 2 < raw.size() &&
                           std::isxdigit(static_cast<unsigned char>(raw[index + 1])) != 0 &&
                           std::isxdigit(static_cast<unsigned char>(raw[index + 2])) != 0;
    if (!is_escape) {
      decoded.push_back(raw[index]);
      continue;
    }
    const auto hex = std::string{raw.substr(index + 1, 2)};
    decoded.push_back(static_cast<char>(std::stoi(hex, nullptr, 16)));
    index += 2;
  }
  return decoded;
}

}  // namespace cms::api
