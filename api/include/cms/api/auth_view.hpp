#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <cms/api/web.hpp>

namespace cms::api {

/// @brief The canned message the sign-in page shows above its form.
///
/// The route maps a query string flag or a completed verification to one of these; no request-supplied text
/// ever becomes page copy.
enum class SigninNotice : std::uint8_t { none, registered, verified, approval_pending };

/// @brief The localized text for @p notice, empty for `none`.
[[nodiscard]] std::string signin_notice(const Locale& locale, SigninNotice notice);

/// @brief Builds the model `signin.html` renders from.
///
/// @param locale The locale to render in.
/// @param site_name The installation's own name.
/// @param message The canned message to show above the form; empty on a first visit.
/// @return The model. @p message is a phrase this service chose, never a value a request supplied.
[[nodiscard]] nlohmann::json signin_model(const Locale& locale, std::string_view site_name,
                                          std::string_view message);

/// @brief Builds the model `signup.html` renders from.
///
/// @param locale The locale to render in.
/// @param site_name The installation's own name.
/// @param message The canned message to show above the form; empty on a first visit.
/// @return The model. Like the sign-in page it carries no field the request filled, so a refused signup is
/// shown a sentence and never the password that was typed.
[[nodiscard]] nlohmann::json signup_model(const Locale& locale, std::string_view site_name,
                                          std::string_view message);

}  // namespace cms::api
