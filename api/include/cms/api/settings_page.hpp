#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <cms/api/settings_view.hpp>
#include <cms/api/web.hpp>
#include <cms/settings_snapshot.hpp>

namespace cms::api {

/// @brief The installation's own name, as the settings row publishes it.
///
/// @return The stored `site.name`, or an empty string when the row is absent, because a heading with no name
/// is still a page.
[[nodiscard]] std::string site_name_of(const SettingsStore& store);

/// @brief Builds the model `settings.html` renders from.
///
/// @param locale The locale to render in.
/// @param site_name The installation's own name.
/// @param settings The visible settings, already redacted by `visible_settings`.
/// @param requested_group The group the query asked for; the first group when it names none or an unknown one.
/// @return The model for one group. A secret's value is replaced with an empty string here as well as being
/// redacted upstream, so a template that lost its tier branch still could not render one.
///
/// A setting's label is its key: the generated catalogue carries no description field, and inventing prose
/// would put words on the admin page that no source in this repository authored.
[[nodiscard]] nlohmann::json settings_model(const Locale& locale, std::string_view site_name,
                                            const std::vector<VisibleSetting>& settings,
                                            std::string_view requested_group);

}  // namespace cms::api
