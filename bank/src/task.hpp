#pragma once

#include <filesystem>

#include <cms/bank/bank.hpp>
#include <cms/error.hpp>

namespace cms::bank {

/// @brief Reads one task directory: `task.json`, its statements, its examples and its solution.
///
/// @param directory The `content/tasks/<id>` directory.
/// @return The task with its content hash filled in, or the first thing wrong with it.
[[nodiscard]] auto load_task(const std::filesystem::path& directory) -> Result<Task>;

}  // namespace cms::bank
