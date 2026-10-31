#pragma once

#include <filesystem>
#include <vector>

#include <cms/bank/bank.hpp>
#include <cms/error.hpp>

namespace cms::bank {

/// @brief Reads `tracks.json`.
///
/// @param path The file to read.
/// @param topics The vocabulary every stage tag must resolve through.
/// @return The tracks in the order the file lists them, each with its digest.
///
/// Order is the data's, not a code constant: the array position becomes the track's position, so
/// reordering routes is an edit to a JSON file.
[[nodiscard]] auto load_tracks(const std::filesystem::path& path, const std::vector<Topic>& topics)
    -> Result<std::vector<Track>>;

}  // namespace cms::bank
