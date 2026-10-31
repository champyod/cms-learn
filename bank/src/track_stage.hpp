#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include <simdjson.h>

#include <cms/bank/bank.hpp>
#include <cms/error.hpp>

namespace cms::bank {

/// @brief Reads one stage object from `tracks.json`.
/// @param position The stage's place in its route, taken from the array.
[[nodiscard]] auto parse_stage(const simdjson::dom::element& element,
                               const std::vector<Topic>& topics, std::string_view where,
                               std::int32_t position) -> Result<Stage>;

}  // namespace cms::bank
