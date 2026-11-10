#pragma once

#include <memory>
#include <string_view>

#include <cms/db/connection.hpp>
#include <cms/settings_snapshot.hpp>

namespace cms::api {

/// @brief Registers `GET /progress`, the learner's own record of what they have done.
///
/// @param store The settings the page shell reads its site name from.
/// @param database_config The pool the session and the learner's rows are read through.
/// @param template_root The directory the template is rendered from.
///
/// The page reads the session's own learner id and nothing else, so one learner cannot ask for another's
/// rows. An unauthenticated request is redirected to `/signin` — it is unauthenticated, not forbidden — and
/// an administrator is refused with 403, because no administrator row belongs in this page.
auto register_progress_route(const std::shared_ptr<SettingsStore>& store,
                            const std::shared_ptr<const db::connection_config>& database_config,
                            std::string_view template_root) -> void;

}  // namespace cms::api
