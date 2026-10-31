#include "tracks.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <simdjson.h>

#include "detail.hpp"
#include "track_stage.hpp"

namespace cms::bank {
namespace {

using detail::Hasher;
using detail::fail;
using detail::nested;
using detail::required_integer;
using detail::required_text;
using detail::with_json;

/// A band is what decides which tasks a stage can serve, so an out-of-range or inverted band is refused
/// here rather than stored and later matching nothing.

[[nodiscard]] auto parse_titles(const simdjson::dom::object& track, std::string_view where,
                                Track& out) -> Result<void> {
  const auto title = nested(track, "title");
  if (!title) {
    return std::unexpected(fail(where, "title is missing"));
  }
  const auto english = required_text(*title, "en", where);
  if (!english) {
    return std::unexpected(english.error());
  }
  const auto thai = required_text(*title, "th", where);
  if (!thai) {
    return std::unexpected(thai.error());
  }
  out.title_en = *english;
  out.title_th = *thai;

  const auto description = nested(track, "description");
  if (!description) {
    return std::unexpected(fail(where, "description is missing"));
  }
  const auto description_en = required_text(*description, "en", where);
  if (!description_en) {
    return std::unexpected(description_en.error());
  }
  const auto description_th = required_text(*description, "th", where);
  if (!description_th) {
    return std::unexpected(description_th.error());
  }
  out.description_en = *description_en;
  out.description_th = *description_th;
  return {};
}



/// The digest is over the values, not the file: a whitespace edit changes nothing, and a renamed stage
/// changes the track's version.
[[nodiscard]] auto digest_track(const Track& track) -> std::string {
  auto hasher = Hasher{};
  hasher.add_field(track.id);
  hasher.add_field(track.title_en);
  hasher.add_field(track.title_th);
  hasher.add_field(track.description_en);
  hasher.add_field(track.description_th);
  for (const auto& stage : track.stages) {
    hasher.add_field(stage.id);
    hasher.add_field(std::to_string(stage.position));
    hasher.add_field(stage.title_en);
    hasher.add_field(stage.title_th);
    hasher.add_field(std::to_string(stage.difficulty_min));
    hasher.add_field(std::to_string(stage.difficulty_max));
    for (const auto& tag : stage.tags) {
      hasher.add_field(tag);
    }
  }
  return hasher.digest();
}

[[nodiscard]] auto parse_track(const simdjson::dom::element& element,
                               const std::vector<Topic>& topics, std::string_view where)
    -> Result<Track> {
  auto object = simdjson::dom::object{};
  if (element.get_object().get(object) != simdjson::SUCCESS) {
    return std::unexpected(fail(where, "a track is not an object"));
  }
  auto track = Track{};

  const auto id = required_text(object, "id", where);
  if (!id) {
    return std::unexpected(id.error());
  }
  track.id = *id;

  const auto titles = parse_titles(object, where, track);
  if (!titles) {
    return std::unexpected(titles.error());
  }

  auto array = simdjson::dom::array{};
  if (object.at_key("stages").get_array().get(array) != simdjson::SUCCESS) {
    return std::unexpected(fail(where, "a track has no stages"));
  }
  auto position = std::int32_t{0};
  for (const auto entry : array) {
    ++position;
    auto stage = parse_stage(entry, topics, where, position);
    if (!stage) {
      return std::unexpected(stage.error());
    }
    track.stages.push_back(std::move(*stage));
  }
  if (track.stages.empty()) {
    return std::unexpected(fail(where, "a track has no stages"));
  }
  track.content_hash = digest_track(track);
  return track;
}

}  // namespace

auto load_tracks(const std::filesystem::path& path, const std::vector<Topic>& topics)
    -> Result<std::vector<Track>> {
  auto tracks = std::vector<Track>{};
  const auto parsed = with_json(path, [&](const simdjson::dom::object& object) {
    auto array = simdjson::dom::array{};
    if (object.at_key("tracks").get_array().get(array) != simdjson::SUCCESS) {
      return Result<void>{std::unexpected(fail(path.string(), "tracks is missing"))};
    }
    for (const auto element : array) {
      auto track = parse_track(element, topics, path.string());
      if (!track) {
        return Result<void>{std::unexpected(track.error())};
      }
      track->position = static_cast<std::int32_t>(tracks.size()) + 1;
      tracks.push_back(std::move(*track));
    }
    return Result<void>{};
  });
  if (!parsed) {
    return std::unexpected(parsed.error());
  }
  if (tracks.empty()) {
    return std::unexpected(fail(path.string(), "the curriculum holds no track"));
  }
  return tracks;
}

}  // namespace cms::bank
