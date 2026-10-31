#include <cms/bank/curriculum.hpp>

#include <cstdint>
#include <exception>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <sqlpp23/sqlpp23.h>

#include <cms/db/tables.hpp>

namespace cms::bank {
namespace {

/// A stage key, so a tag cannot be attached to the wrong route's stage of the same name.
using StageKey = std::pair<std::string, std::string>;

auto read_tracks(db::connection& database, std::vector<Track>& tracks,
                 std::map<std::string, std::size_t>& index) {
  for (const auto& row : database(sqlpp::select(db::tracks.id, db::tracks.titleEn,
                                                db::tracks.titleTh, db::tracks.descriptionEn,
                                                db::tracks.descriptionTh)
                                      .from(db::tracks)
                                      .order_by(db::tracks.position.asc()))) {
    auto track = Track{};
    track.id = std::string{row.id};
    track.title_en = std::string{row.titleEn};
    track.title_th = std::string{row.titleTh};
    track.description_en = std::string{row.descriptionEn};
    track.description_th = std::string{row.descriptionTh};
    track.position = static_cast<std::int32_t>(tracks.size()) + 1;
    index[track.id] = tracks.size();
    tracks.push_back(std::move(track));
  }
}

auto read_stages(db::connection& database, std::vector<Track>& tracks,
                 const std::map<std::string, std::size_t>& index,
                 std::map<StageKey, std::size_t>& stage_index) {
  for (const auto& row :
       database(sqlpp::select(db::trackStages.trackId, db::trackStages.id,
                              db::trackStages.titleEn, db::trackStages.titleTh,
                              db::trackStages.difficultyMin, db::trackStages.difficultyMax)
                    .from(db::trackStages)
                    .order_by(db::trackStages.trackId.asc(), db::trackStages.position.asc()))) {
    const auto owner = index.find(std::string{row.trackId});
    if (owner == index.end()) {
      continue;
    }
    auto& track = tracks[owner->second];
    auto stage = Stage{};
    stage.id = std::string{row.id};
    stage.title_en = std::string{row.titleEn};
    stage.title_th = std::string{row.titleTh};
    stage.difficulty_min = static_cast<std::int32_t>(row.difficultyMin);
    stage.difficulty_max = static_cast<std::int32_t>(row.difficultyMax);
    stage.position = static_cast<std::int32_t>(track.stages.size()) + 1;
    stage_index[StageKey{std::string{row.trackId}, stage.id}] = track.stages.size();
    track.stages.push_back(std::move(stage));
  }
}

auto read_stage_tags(db::connection& database, std::vector<Track>& tracks,
                     const std::map<std::string, std::size_t>& index,
                     const std::map<StageKey, std::size_t>& stage_index) {
  for (const auto& row : database(sqlpp::select(db::stageTags.trackId, db::stageTags.stageId,
                                                db::stageTags.tag)
                                      .from(db::stageTags)
                                      .order_by(db::stageTags.tag.asc()))) {
    const auto owner = index.find(std::string{row.trackId});
    if (owner == index.end()) {
      continue;
    }
    const auto stage = stage_index.find(StageKey{std::string{row.trackId}, std::string{row.stageId}});
    if (stage == stage_index.end()) {
      continue;
    }
    tracks[owner->second].stages[stage->second].tags.emplace_back(row.tag);
  }
}

}  // namespace

auto read_curriculum(db::connection& database) -> Result<std::vector<Track>> {
  auto tracks = std::vector<Track>{};
  auto index = std::map<std::string, std::size_t>{};
  auto stage_index = std::map<StageKey, std::size_t>{};
  try {
    read_tracks(database, tracks, index);
    read_stages(database, tracks, index, stage_index);
    read_stage_tags(database, tracks, index, stage_index);
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }
  return tracks;
}

}  // namespace cms::bank
