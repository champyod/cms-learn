#pragma once

#include <memory>
#include <string_view>

#include <cms/api/settings_view.hpp>
#include <cms/db/connection.hpp>
#include <cms/settings_snapshot.hpp>

namespace cms::api {

/// @brief Registers `GET /`, the curriculum page.
///
/// @param store The settings the page shell reads its site name from.
/// @param database_config The pool the curriculum is read through.
/// @param template_root The directory the template is rendered from.
///
/// The curriculum is read per request, so a route imported into the database appears without a restart and
/// an unreachable database is reported rather than served as a stale page.
auto register_home_route(const std::shared_ptr<SettingsStore>& store,
                         const std::shared_ptr<const db::connection_config>& database_config,
                         std::string_view template_root) -> void;

/// @brief Registers `GET /tasks/{id}`, the task page.
///
/// @param store The settings the page shell reads its site name from.
/// @param database_config The pool the task is read through.
/// @param template_root The directory the template is rendered from.
///
/// The task is read through the published-and-visible predicate, and an unknown, a draft and a hidden task
/// all answer 404 with the same body, so the route cannot be used to enumerate hidden ids.
auto register_task_route(const std::shared_ptr<SettingsStore>& store,
                         const std::shared_ptr<const db::connection_config>& database_config,
                         std::string_view template_root) -> void;

/// @brief Registers `GET /settings`, the admin settings page.
///
/// @param store The settings the page shell and the rendered values are read from.
/// @param database_config The pool the session and its authority are read through.
/// @param view The cached visible-settings list; a secret is already redacted in it.
/// @param template_root The directory the template is rendered from.
///
/// An unauthenticated request is redirected to `/signin`: it is not forbidden, it is unauthenticated. A
/// session without `settings_read` is answered 403.
auto register_settings_route(const std::shared_ptr<SettingsStore>& store,
                             const std::shared_ptr<const db::connection_config>& database_config,
                             const std::shared_ptr<const SettingsView>& view,
                             std::string_view template_root) -> void;

}  // namespace cms::api
