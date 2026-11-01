#pragma once

#include <string>
#include <string_view>
#include <vector>

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

/// @brief Renders the curriculum page.
///
/// @param template_root The directory holding `home.html` and `layout.html`.
/// @param locale The locale to render in.
/// @param tracks The curriculum to list.
/// @param site_name The installation's own name.
/// @return The HTML, or the reason it could not be produced.
[[nodiscard]] Result<std::string> render_home(const std::string& template_root,
                                              const Locale& locale,
                                              const std::vector<bank::Track>& tracks,
                                              std::string_view site_name);

}  // namespace cms::api
