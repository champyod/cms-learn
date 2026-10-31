#include "track_stage.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <simdjson.h>

#include "detail.hpp"

namespace cms::bank {
namespace {

using detail::fail;
using detail::nested;
using detail::required_integer;
using detail::required_text;

constexpr std::int64_t MINIMUM_DIFFICULTY = 800;
constexpr std::int64_t MAXIMUM_DIFFICULTY = 3500;
constexpr std::int64_t DIFFICULTY_STEP = 100;

[[nodiscard]] auto check_band(std::int64_t minimum, std::int64_t maximum, std::string_view where)
    -> Result<void> {
  if (minimum < MINIMUM_DIFFICULTY || maximum > MAXIMUM_DIFFICULTY || minimum > maximum ||
      minimum % DIFFICULTY_STEP != 0 || maximum % DIFFICULTY_STEP != 0) {
    return std::unexpected(fail(where, "the difficulty band is not a multiple-of-100 range within "
                                       "800 to 3500, or its ends are inverted"));
  }
  return {};
}


[[nodiscard]] auto parse_stage_tag(const simdjson::dom::element& element,
                                   const std::vector<Topic>& topics, std::string_view where,
                                   Stage& stage) -> Result<void> {
  auto tag = std::string_view{};
  if (element.get_string().get(tag) != simdjson::SUCCESS) {
    return std::unexpected(fail(where, "a stage tag is not a string"));
  }
  const auto found = std::ranges::find(topics, tag, &Topic::id);
  if (found == topics.end()) {
    return std::unexpected(fail(where, "tag \"" + std::string{tag} + "\" is not in topics.json"));
  }
  stage.tags.emplace_back(tag);
  return {};
}



}  // namespace

[[nodiscard]] auto parse_stage(const simdjson::dom::element& element,
                               const std::vector<Topic>& topics, std::string_view where,
                               std::int32_t position) -> Result<Stage> {
  auto object = simdjson::dom::object{};
  if (element.get_object().get(object) != simdjson::SUCCESS) {
    return std::unexpected(fail(where, "a stage is not an object"));
  }
  auto stage = Stage{};
  stage.position = position;

  const auto id = required_text(object, "id", where);
  if (!id) {
    return std::unexpected(id.error());
  }
  stage.id = *id;

  const auto title = nested(object, "title");
  if (!title) {
    return std::unexpected(fail(where, "a stage title is missing"));
  }
  const auto english = required_text(*title, "en", where);
  if (!english) {
    return std::unexpected(english.error());
  }
  const auto thai = required_text(*title, "th", where);
  if (!thai) {
    return std::unexpected(thai.error());
  }
  stage.title_en = *english;
  stage.title_th = *thai;

  const auto difficulty = nested(object, "difficulty");
  if (!difficulty) {
    return std::unexpected(fail(where, "a stage difficulty is missing"));
  }
  const auto minimum = required_integer(*difficulty, "min", where);
  if (!minimum) {
    return std::unexpected(minimum.error());
  }
  const auto maximum = required_integer(*difficulty, "max", where);
  if (!maximum) {
    return std::unexpected(maximum.error());
  }
  const auto band = check_band(*minimum, *maximum, where);
  if (!band) {
    return std::unexpected(band.error());
  }
  stage.difficulty_min = static_cast<std::int32_t>(*minimum);
  stage.difficulty_max = static_cast<std::int32_t>(*maximum);

  auto array = simdjson::dom::array{};
  if (object.at_key("tags").get_array().get(array) != simdjson::SUCCESS) {
    return std::unexpected(fail(where, "a stage has no tags"));
  }
  for (const auto tag : array) {
    const auto added = parse_stage_tag(tag, topics, where, stage);
    if (!added) {
      return std::unexpected(added.error());
    }
  }
  if (stage.tags.empty()) {
    return std::unexpected(fail(where, "a stage has no tags"));
  }
  return stage;
}


}  // namespace cms::bank
