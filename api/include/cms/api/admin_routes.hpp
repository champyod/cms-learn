#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <cms/api/actor.hpp>
#include <cms/api/task_plan.hpp>
#include <cms/object_store.hpp>
#include <cms/api/admin_actions.hpp>
#include <cms/api/web.hpp>
#include <cms/db/connection.hpp>
#include <cms/error.hpp>
#include <cms/settings_snapshot.hpp>

namespace cms::api {

/// @brief One learner, as the panel's home page lists it.
struct LearnerRow {
  std::string id;
  std::string handle;
  std::string status;
  std::string level;
};

/// @brief The learners the panel lists, in handle order.
[[nodiscard]] Result<std::vector<LearnerRow>> read_learner_roster(db::connection& database);

/// @brief The handle the panel greets, read from the administrator the session names.
[[nodiscard]] Result<std::string> read_admin_handle(db::connection& database,
                                                    std::string_view admin_id);

/// @brief One role the grant form can offer.
struct RoleRow {
  std::string id;
  std::string name;
};

/// @brief One administrator the grant form can offer.
struct AdminRow {
  std::string id;
  std::string handle;
};

/// @brief The roles a grant can name, in name order.
[[nodiscard]] Result<std::vector<RoleRow>> read_roles(db::connection& database);

/// @brief The administrators a grant can name, in handle order.
[[nodiscard]] Result<std::vector<AdminRow>> read_administrators(db::connection& database);

struct PanelHome {
  std::string admin_handle;
  std::string csrf_token;
  std::string notice;
  std::string message;
  std::vector<LearnerRow> learners;
  std::vector<RoleRow> roles;
  std::vector<AdminRow> administrators;
};

/// @brief Builds the model `admin_home.html` renders from.
[[nodiscard]] nlohmann::json panel_model(const Locale& locale, std::string_view site_name,
                                         const PanelHome& home);

/// @brief The five actions the panel's forms can post.
enum class PanelAction : std::uint8_t {
  create_learner,
  create_admin,
  approve,
  revoke_admin,
  issue_verification,
  grant_role,
  save_task_draft,
};

/// @brief Runs @p action as @p actor, reading only that action's own fields from @p body.
///
/// The body cannot name the actor: @p actor is the account the session resolved to, and the fields read
/// here are the ones the form shows.
[[nodiscard]] Result<ActionOutcome> perform_panel_action(db::connection& database,
                                                         const SettingsSnapshot& settings,
                                                         const Actor& actor, PanelAction action,
                                                         std::string_view body);

/// @brief What the authoring form shows: the types, and the fields of the one that was chosen.
struct TaskForm {
  std::string csrf_token;
  std::string notice;
  std::string message;
  std::vector<TaskTypeRow> types;
  std::string selected_type;
};

/// @brief Builds the model `admin_task_form.html` renders from. Every key the template reads is set here.
[[nodiscard]] nlohmann::json task_form_model(const Locale& locale, std::string_view site_name,
                                             const TaskForm& form);

/// @brief Registers the administrator panel: the home page and the actions it posts.
///
/// @param database_config The pool sessions, authority and the roster are read through.
/// @param store The settings the account actions read their policy from.
/// @param template_root The directory the page is rendered from.
/// @param panel_port The listener port the panel was mounted on; a request arriving on any other port is
/// refused, which is what keeps the panel off the public listener.
///
/// The acting account is the one the session names, never a field of the request. A request without a
/// live administrator session is redirected to `/signin`, and one whose session is not an administrator
/// is refused. Every POST compares the session's token before any action runs.
/// @param store_config The object store the task bytes live in; empty means no store is configured, and a
/// request that needs one is refused rather than served from nothing.
auto register_admin_routes(const std::shared_ptr<const db::connection_config>& database_config,
                           const std::shared_ptr<SettingsStore>& store,
                           std::string_view template_root, std::uint16_t panel_port,
                           const store::StoreConfig& store_config = {}) -> void;

}  // namespace cms::api
