#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <cms/bank/bank.hpp>
#include <cms/error.hpp>

namespace cms::api {

/// @brief The two locales the site is authored in.
struct Locale {
  /// The locale the page is rendered in.
  std::string code{"th"};
  /// The locale the switch offers.
  std::string other{"en"};
};

/// @brief The locale a request wants.
///
/// @param query_language The `lang` query parameter, empty when absent.
/// @param accept_language The `Accept-Language` header, empty when absent.
/// @return Thai unless the request asks for English, because the site's readers are primarily Thai and a
/// page in the wrong language is worse than a page in the default one.
[[nodiscard]] Locale choose_locale(std::string_view query_language,
                                   std::string_view accept_language);

/// @brief The label to show for @p locale.
///
/// @return @p thai when the locale is Thai and that label exists, otherwise @p english when it exists,
/// otherwise @p thai. A missing translation falls back to the other locale rather than rendering an empty
/// heading: content is authored by people, and one absent field must not blank a page.
[[nodiscard]] std::string label(const Locale& locale, std::string_view english,
                                std::string_view thai);

/// @brief Builds the model `home.html` renders from.
///
/// @param locale The locale to render in.
/// @param tracks The curriculum to list.
/// @param site_name The installation's own name.
/// @return The model, with every word the locale cannot supply replaced by the other locale's.
[[nodiscard]] nlohmann::json home_model(const Locale& locale,
                                        const std::vector<bank::Track>& tracks,
                                        std::string_view site_name);

/// @brief Renders one template of @p template_root against @p model.
///
/// @param template_root The directory holding the templates and their partials.
/// @param page The template file to render, relative to @p template_root.
/// @param model The values the template reads.
/// @return The HTML, or the reason it could not be produced.
///
/// The model is the caller's, so a page's data is decided where the page's data comes from rather
/// than by a renderer that would have to know every page.
[[nodiscard]] Result<std::string> render_page(const std::string& template_root,
                                              std::string_view page,
                                              const nlohmann::json& model);

/// @brief The file a request for @p relative under @p static_root may serve.
///
/// @param static_root The directory the operator published assets in.
/// @param relative The path the request named, with no leading separator.
/// @return The path to serve, or nothing when it is outside @p static_root or is not a regular file.
///
/// The candidate is resolved before it is compared, so `..`, an absolute path and a symlink out of the
/// tree all land outside the root and are refused by the same check instead of by three string rules.
[[nodiscard]] std::optional<std::string> static_file(std::string_view static_root,
                                                     std::string_view relative);

}  // namespace cms::api
