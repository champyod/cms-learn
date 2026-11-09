#include "admin_protocol_internal.hpp"

#include <string>
#include <string_view>
#include <vector>

#include <cms/api/admin_actions.hpp>

namespace cms::api::detail {
namespace {

constexpr std::string_view ROLE_SEPARATOR = ",";

auto answered(const Result<ActionOutcome>& outcome) -> std::string {
  if (!outcome) {
    return refused(outcome.error());
  }
  return accepted(",\"subject\":\"" + escape(outcome->subject) + "\",\"detail\":\"" +
                  escape(outcome->detail) + "\"");
}

auto split_roles(std::string_view roles) -> std::vector<std::string> {
  auto ids = std::vector<std::string>{};
  while (!roles.empty()) {
    const auto separator = roles.find(ROLE_SEPARATOR);
    const auto role = roles.substr(0, separator);
    if (!role.empty()) {
      ids.emplace_back(role);
    }
    if (separator == std::string_view::npos) {
      break;
    }
    roles.remove_prefix(separator + ROLE_SEPARATOR.size());
  }
  return ids;
}

}  // namespace

auto answer_create_learner(const Request& request, const SettingsSnapshot& settings,
                           db::connection& database, const Actor& caller) -> std::string {
  if (!require(request, "handle")) {
    return refused("creating a learner needs handle");
  }
  return answered(create_learner(
      database, settings, caller,
      CreateLearnerRequest{.handle = std::string{text_of(request, "handle")},
                           .email = std::string{text_of(request, "email")},
                           .level_id = std::string{text_of(request, "level_id")},
                           .password = std::string{text_of(request, "password")}}));
}

auto answer_create_admin(const Request& request, const SettingsSnapshot& settings,
                         db::connection& database, const Actor& caller) -> std::string {
  if (!require(request, "handle")) {
    return refused("creating an administrator needs handle");
  }
  return answered(create_admin(
      database, settings, caller,
      CreateAdminRequest{.handle = std::string{text_of(request, "handle")},
                         .role_ids = split_roles(text_of(request, "roles")),
                         .password = std::string{text_of(request, "password")}}));
}

auto answer_approve_learner(const Request& request, db::connection& database, const Actor& caller)
    -> std::string {
  if (!require(request, "learner_id")) {
    return refused("approving a learner needs learner_id");
  }
  return answered(approve_learner(database, caller, text_of(request, "learner_id")));
}

auto answer_issue_verification(const Request& request, const SettingsSnapshot& settings,
                               db::connection& database, const Actor& caller) -> std::string {
  if (!require(request, "learner_id")) {
    return refused("issuing a verification needs learner_id");
  }
  return answered(
      issue_verification_for(database, settings, caller, text_of(request, "learner_id")));
}

auto answer_revoke_admin(const Request& request, db::connection& database, const Actor& caller)
    -> std::string {
  if (!require(request, "admin_id")) {
    return refused("revoking an administrator needs admin_id");
  }
  return answered(revoke_admin(database, caller, text_of(request, "admin_id")));
}

}  // namespace cms::api::detail
