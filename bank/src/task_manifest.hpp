#pragma once

#include <filesystem>

#include <cms/bank/bank.hpp>
#include <cms/error.hpp>

namespace cms::bank {

/// @brief Reads `task.json` into @p task and checks the fields the schema cannot.
/// @return The first thing wrong with the manifest, naming the field.
[[nodiscard]] auto parse_manifest(const std::filesystem::path& directory, Task& task)
    -> Result<void>;

}  // namespace cms::bank
