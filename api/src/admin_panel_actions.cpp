#include <cms/api/admin_routes.hpp>

#include <string>
#include <string_view>
#include <vector>

#include <cms/api/form_body.hpp>
#include <cms/api/task_plan.hpp>
#include <cms/error.hpp>

namespace cms::api {
namespace {

constexpr std::string_view ROLE_SEPARATOR = ",";

auto role_ids_of(std::string_view field) -> std::vector<std::string> {
  auto ids = std::vector<std::string>{};
  while (!field.empty()) {
    const auto separator = field.find(ROLE_SEPARATOR);
    const auto id = field.substr(0, separator);
    if (!id.empty()) {
      ids.emplace_back(id);
    }
    if (separator == std::string_view::npos) {
      break;
    }
    field.remove_prefix(separator + ROLE_SEPARATOR.size());
  }
  return ids;
}

}  // namespace

auto perform_panel_action(db::connection& database, const SettingsSnapshot& settings,
                          const Actor& actor, PanelAction action, std::string_view body)
    -> Result<ActionOutcome> {
  switch (action) {
    case PanelAction::create_learner:
      return create_learner(database, settings, actor,
                            CreateLearnerRequest{.handle = form_field(body, "handle"),
                                                 .email = form_field(body, "email"),
                                                 .level_id = form_field(body, "level_id"),
                                                 .password = form_field(body, "password")});
    case PanelAction::create_admin:
      return create_admin(database, settings, actor,
                          CreateAdminRequest{.handle = form_field(body, "handle"),
                                             .role_ids = role_ids_of(form_field(body, "roles")),
                                             .password = form_field(body, "password")});
    case PanelAction::approve:
      return approve_learner(database, actor, form_field(body, "learner_id"));
    case PanelAction::revoke_admin:
      return revoke_admin(database, actor, form_field(body, "admin_id"));
    case PanelAction::grant_role:
      return grant_role(database, actor, GrantRoleRequest{.admin_id = form_field(body, "admin_id"),
                                                          .role_id = form_field(body, "role_id")});
    case PanelAction::save_task_draft: {
      const auto saved = save_task_draft(database, actor, body);
      if (!saved) {
        return std::unexpected(saved.error());
      }
      return ActionOutcome{.subject = *saved, .detail = "saved the draft"};
    }
    case PanelAction::issue_verification:
      return issue_verification_for(database, settings, actor, form_field(body, "learner_id"));
  }
  return std::unexpected(Error::make(ErrorCode::internal, "unknown panel action"));
}

}  // namespace cms::api
