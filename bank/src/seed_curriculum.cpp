#include "seed_curriculum.hpp"

#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <utility>

#include <sqlpp23/sqlpp23.h>

#include <cms/db/tables.hpp>

namespace cms::bank {
namespace {

struct ExistingTrack {
  std::string content_hash;
  bool is_seeded{false};
};

auto read_tracks(db::connection& database) -> std::map<std::string, ExistingTrack> {
  auto existing = std::map<std::string, ExistingTrack>{};
  for (const auto& row : database(sqlpp::select(db::tracks.id, db::tracks.contentHash,
                                                db::tracks.seeded)
                                      .from(db::tracks))) {
    existing[std::string{row.id}] =
        ExistingTrack{.content_hash = std::string{row.contentHash}, .is_seeded = row.seeded};
  }
  return existing;
}

auto write_track_row(db::connection& database, const Track& track, bool is_update) {
  if (is_update) {
    database(sqlpp::update(db::tracks)
                 .set(db::tracks.position = track.position, db::tracks.titleEn = track.title_en,
                      db::tracks.titleTh = track.title_th,
                      db::tracks.descriptionEn = track.description_en,
                      db::tracks.descriptionTh = track.description_th,
                      db::tracks.contentHash = track.content_hash)
                 .where(db::tracks.id == track.id));
    // Stage tags cascade from the stage rows, so removing the stages is enough.
    database(sqlpp::delete_from(db::trackStages)
                 .where(db::trackStages.trackId == track.id));
    return;
  }
  database(sqlpp::insert_into(db::tracks).set(
      db::tracks.id = track.id, db::tracks.position = track.position,
      db::tracks.titleEn = track.title_en, db::tracks.titleTh = track.title_th,
      db::tracks.descriptionEn = track.description_en,
      db::tracks.descriptionTh = track.description_th, db::tracks.contentHash = track.content_hash,
      db::tracks.seeded = true));
}

auto write_stages(db::connection& database, const Track& track) {
  for (const auto& stage : track.stages) {
    database(sqlpp::insert_into(db::trackStages).set(
        db::trackStages.trackId = track.id, db::trackStages.id = stage.id,
        db::trackStages.position = stage.position, db::trackStages.titleEn = stage.title_en,
        db::trackStages.titleTh = stage.title_th,
        db::trackStages.difficultyMin = stage.difficulty_min,
        db::trackStages.difficultyMax = stage.difficulty_max));
    for (const auto& tag : stage.tags) {
      database(sqlpp::insert_into(db::stageTags).set(
          db::stageTags.trackId = track.id, db::stageTags.stageId = stage.id,
          db::stageTags.tag = tag));
    }
  }
}

/// A route the data no longer declares is removed, but only when the seeder owns it. Renaming a track in
/// the file therefore replaces it; an administrator's own route is left alone.
auto drop_absent_tracks(db::connection& database, const std::map<std::string, ExistingTrack>& existing,
                        const Bank& bank) -> std::size_t {
  auto declared = std::set<std::string>{};
  for (const auto& track : bank.tracks) {
    declared.insert(track.id);
  }
  auto removed = std::size_t{0};
  for (const auto& [id, row] : existing) {
    if (row.is_seeded && !declared.contains(id)) {
      database(sqlpp::delete_from(db::tracks).where(db::tracks.id == id));
      ++removed;
    }
  }
  return removed;
}

}  // namespace

auto import_curriculum(db::connection& database, const Bank& bank) -> Result<std::size_t> {
  try {
    const auto existing = read_tracks(database);
    auto written = std::size_t{0};
    for (const auto& track : bank.tracks) {
      const auto found = existing.find(track.id);
      if (found != existing.end() && !found->second.is_seeded) {
        continue;
      }
      if (found != existing.end() && found->second.content_hash == track.content_hash) {
        continue;
      }
      write_track_row(database, track, found != existing.end());
      write_stages(database, track);
      ++written;
    }
    (void)drop_absent_tracks(database, existing, bank);
    return written;
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }
}

}  // namespace cms::bank
