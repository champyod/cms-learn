#pragma once

#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <cms/api/web.hpp>

namespace cms::api {

/// @brief What the verdict fragment shows: either "nothing yet" or the verdict the judge recorded.
struct VerdictView {
  bool is_pending{true};
  /// The stored `submissions.verdict` value; meaningful only when @ref is_pending is false.
  std::string verdict;
};

/// @brief Builds the model the verdict fragment renders from.
///
/// @param locale The locale to render in.
/// @param task_id The task whose page polls this fragment.
/// @param view The verdict, or that none has arrived.
/// @return The model. Every value is escaped by the renderer, so the task id is safe in the `hx-get` path.
[[nodiscard]] nlohmann::json verdict_fragment_model(const Locale& locale,
                                                    std::string_view task_id,
                                                    const VerdictView& view);

/// @brief The fragment source for the pending or the finished shape.
///
/// The pending shape keeps the `hx-get`/`hx-trigger`/`hx-swap` attributes, so the page keeps polling; the
/// finished shape drops them, so the swap stops asking. They live here rather than in `design/` because the
/// port generates whole pages and cannot emit a one-element fragment.
[[nodiscard]] std::string_view verdict_fragment_source(bool is_pending);

}  // namespace cms::api
