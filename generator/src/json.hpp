#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <simdjson.h>

#include <cms/error.hpp>

namespace cms::generate::detail {

/// @brief Parses @p text as one JSON object and hands it to @p use before the document it lives in dies.
///
/// A plan stage's answer is JSON because generation_candidates stores the plan as jsonb, so the shape is
/// refused here with the call it arrived in rather than by the database one row later.
template <typename Use>
auto with_object(std::string_view text, std::string_view what, Use&& use) -> Result<void> {
  const auto padded = simdjson::padded_string{text};
  auto parser = simdjson::dom::parser{};
  auto document = simdjson::dom::element{};
  if (parser.parse(padded).get(document) != simdjson::SUCCESS) {
    return std::unexpected(
        Error::make(ErrorCode::invalid_argument, std::string{what} + " is not valid JSON"));
  }
  auto object = simdjson::dom::object{};
  if (document.get_object().get(object) != simdjson::SUCCESS) {
    return std::unexpected(
        Error::make(ErrorCode::invalid_argument, std::string{what} + " is not a JSON object"));
  }
  return use(object);
}

/// @return The string @p name holds, or nothing when the key is absent or not a string.
[[nodiscard]] inline auto field_text(const simdjson::dom::object& object, std::string_view name)
    -> std::optional<std::string> {
  auto view = std::string_view{};
  if (object.at_key(name).get_string().get(view) != simdjson::SUCCESS) {
    return std::nullopt;
  }
  return std::string{view};
}

/// @return The boolean @p name holds, or nothing when the key is absent or not a boolean.
[[nodiscard]] inline auto field_bool(const simdjson::dom::object& object, std::string_view name)
    -> std::optional<bool> {
  auto value = bool{};
  if (object.at_key(name).get_bool().get(value) != simdjson::SUCCESS) {
    return std::nullopt;
  }
  return value;
}

/// @return The integer @p name holds, or nothing when the key is absent or not an integer.
[[nodiscard]] inline auto field_integer(const simdjson::dom::object& object, std::string_view name)
    -> std::optional<std::int64_t> {
  auto value = std::int64_t{0};
  if (object.at_key(name).get_int64().get(value) != simdjson::SUCCESS) {
    return std::nullopt;
  }
  return value;
}

/// @return The object as the smallest JSON text that describes it, which is what a jsonb column stores.
[[nodiscard]] inline auto minified(const simdjson::dom::object& object) -> std::string {
  return simdjson::minify(object);
}

}  // namespace cms::generate::detail
