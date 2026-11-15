#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <cms/error.hpp>

namespace cms::generate {

/// The marker that closes a data block. Text that carries it is refused rather than escaped: a teacher's brief
/// has no reason to name the boundary, and one that does would close the block it is written inside.
constexpr std::string_view DATA_FRAME_CLOSE = "<<<END DATA>>>";

/// The marker that opens a data block, with the label of what follows.
constexpr std::string_view DATA_FRAME_OPEN = "<<<BEGIN DATA: ";

/// @brief One piece of untrusted model input: the text, and the name that says what it is.
struct ModelData {
  std::string label;
  std::string text;
};

/// @brief Renders @p data as data: an opening line that names the boundary and says the block is content, the
/// text verbatim, and the closing marker.
///
/// This is the only place a prompt may carry foreign text. A prompt assembled by pasting a brief or a previous
/// step's answer into an instruction is what ADR 0029 forbids, so the instruction and the data are joined here
/// and nowhere else.
///
/// @param data The label (ours, never the teacher's) and the text.
/// @return The framed block, or a refusal naming the label and the marker the text tried to close.
[[nodiscard]] auto frame_data(const ModelData& data) -> Result<std::string>;

/// @brief Renders several pieces of data, each in its own block, in order.
[[nodiscard]] auto frame_all(const std::vector<ModelData>& data) -> Result<std::string>;

}  // namespace cms::generate
