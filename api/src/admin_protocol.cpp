#include <cms/api/admin_protocol.hpp>

#include "admin_protocol_internal.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <simdjson.h>

#include <cms/api/elevation.hpp>
#include <cms/api/settings_plan.hpp>
#include <cms/error.hpp>
#include <cms/settings_snapshot.hpp>

namespace cms::api::detail {

auto accepted(std::string_view body) -> std::string {
  auto response = std::string{"{\"ok\":true"};
  response += body;
  response += "}\n";
  return response;
}

auto refused(std::string_view reason) -> std::string {
  return "{\"ok\":false,\"error\":\"" + escape(reason) + "\"}\n";
}

auto refused(const Error& error) -> std::string { return refused(error.message); }

auto names_caller(const Request& request) -> std::string_view {
  constexpr auto CALLER_FIELDS =
      std::array<std::string_view, 4>{"granted_by", "approved_by", "actor_id", "author_id"};
  for (const auto name : CALLER_FIELDS) {
    if (present(request, name)) {
      return name;
    }
  }
  return {};
}

}  // namespace cms::api::detail

namespace cms::api {
namespace {

using detail::accepted;
using detail::escape;
using detail::number_of;
using detail::present;
using detail::refused;
using detail::require;
using detail::text_of;

auto read_field(detail::Request& request, std::string_view key, simdjson::dom::element value) -> bool {
  switch (value.type()) {
    case simdjson::dom::element_type::STRING: {
      auto text = std::string_view{};
      if (value.get_string().get(text) != simdjson::SUCCESS) {
        return false;
      }
      request.strings.emplace(std::string{key}, std::string{text});
      return true;
    }
    case simdjson::dom::element_type::INT64: {
      auto number = std::int64_t{0};
      if (value.get_int64().get(number) != simdjson::SUCCESS) {
        return false;
      }
      request.numbers.emplace(std::string{key}, number);
      return true;
    }
    default:
      return false;
  }
}

auto parse(const simdjson::padded_string& json) -> detail::Request {
  auto request = detail::Request{};
  auto parser = simdjson::dom::parser{};
  auto document = simdjson::dom::element{};
  if (parser.parse(json).get(document) != simdjson::SUCCESS ||
      document.type() != simdjson::dom::element_type::OBJECT) {
    request.is_valid = false;
    return request;
  }

  auto object = simdjson::dom::object{};
  if (document.get_object().get(object) != simdjson::SUCCESS) {
    request.is_valid = false;
    return request;
  }

  for (const auto [key, value] : object) {
    if (!read_field(request, key, value)) {
      request.is_valid = false;
      return request;
    }
  }
  return request;
}

auto answer_elevation_grant(const detail::Request& request, const SettingsSnapshot& settings,
                            db::connection& database, const Actor& caller) -> std::string {
  for (const auto name : {"principal_id", "capability", "reason"}) {
    if (!require(request, name)) {
      return refused("an elevation needs " + std::string{name});
    }
  }

  const auto outcome = grant_elevation(
      database, settings,
      ElevationRequest{.principal_id = std::string{text_of(request, "principal_id")},
                       .capability = std::string{text_of(request, "capability")},
                       .granted_by = caller,
                       .reason = std::string{text_of(request, "reason")},
                       .minutes = number_of(request, "minutes")});
  if (!outcome) {
    return refused(outcome.error());
  }
  return accepted(",\"elevation_id\":\"" + outcome->id + "\",\"minutes\":" +
                  std::to_string(outcome->minutes) + ",\"live\":" +
                  (outcome->is_live ? "true" : "false"));
}

auto answer_elevation_approve(const detail::Request& request, db::connection& database,
                              const Actor& caller) -> std::string {
  if (!require(request, "elevation_id")) {
    return refused("an approval needs elevation_id");
  }
  const auto outcome = approve_elevation(database, text_of(request, "elevation_id"), caller);
  if (!outcome) {
    return refused(outcome.error());
  }
  return accepted(",\"live\":true");
}

auto answer_settings_plan(const detail::Request& request, const std::vector<CatalogueEntry>& catalogue,
                          db::connection& database, const Actor& caller) -> std::string {
  if (!require(request, "key")) {
    return refused("a plan needs key");
  }
  if (!present(request, "value")) {
    return refused("a plan needs value");
  }

  const auto preview = plan_setting(database, catalogue, text_of(request, "key"),
                                    text_of(request, "value"), caller);
  if (!preview) {
    return refused(preview.error());
  }
  return accepted(",\"plan_id\":\"" + escape(preview->id) + "\",\"key\":\"" +
                  escape(preview->key) + "\",\"old_value\":\"" + escape(preview->old_value) +
                  "\",\"new_value\":\"" + escape(preview->new_value) + "\",\"tier\":\"" +
                  escape(preview->tier) + "\",\"reload\":\"" + escape(preview->reload) +
                  "\",\"expires_at\":\"" + escape(preview->expires_at) + "\"");
}

auto answer_settings_apply(const detail::Request& request,
                           const std::vector<CatalogueEntry>& catalogue,
                           const std::shared_ptr<const db::connection_config>& database_config,
                           const Actor& caller) -> std::string {
  if (!require(request, "plan_id")) {
    return refused("an apply needs plan_id");
  }

  const auto applied =
      apply_plan(database_config, catalogue, text_of(request, "plan_id"), caller);
  if (!applied) {
    return refused(applied.error());
  }
  return accepted(",\"plan_id\":\"" + escape(applied->id) + "\",\"key\":\"" +
                  escape(applied->key) + "\"");
}

}  // namespace

auto dispatch_line(std::string_view request, const SettingsSnapshot& settings,
                   const std::shared_ptr<const db::connection_config>& database_config,
                   const std::vector<CatalogueEntry>& catalogue, const Actor& caller)
    -> std::string {
  // The padded copy owns the bytes every field view points into, so it outlives the parsed request.
  const auto json = simdjson::padded_string{request};
  const auto fields = parse(json);
  if (!fields.is_valid) {
    return refused("the request is not one flat JSON object of strings and integers");
  }

  const auto actor_field = detail::names_caller(fields);
  if (!actor_field.empty()) {
    return refused("the caller is the connection, not the request; a request may not carry " +
                   std::string{actor_field});
  }

  const auto action = text_of(fields, "action");
  if (action == "ping") {
    return accepted(",\"settings_version\":" + std::to_string(settings.version));
  }
  if (action == "status") {
    return accepted(",\"settings_version\":" + std::to_string(settings.version));
  }
  if (action == "settings_apply") {
    return answer_settings_apply(fields, catalogue, database_config, caller);
  }
  auto database = db::connection{database_config};
  if (action == "elevation_grant") {
    return answer_elevation_grant(fields, settings, database, caller);
  }
  if (action == "elevation_approve") {
    return answer_elevation_approve(fields, database, caller);
  }
  if (action == "settings_plan") {
    return answer_settings_plan(fields, catalogue, database, caller);
  }
  if (action == "account_create_learner") {
    return detail::answer_create_learner(fields, settings, database, caller);
  }
  if (action == "account_create_admin") {
    return detail::answer_create_admin(fields, settings, database, caller);
  }
  if (action == "account_approve") {
    return detail::answer_approve_learner(fields, database, caller);
  }
  if (action == "account_issue_verification") {
    return detail::answer_issue_verification(fields, settings, database, caller);
  }
  if (action == "account_revoke_admin") {
    return detail::answer_revoke_admin(fields, database, caller);
  }
  return refused("unknown action: " + std::string{action});
}

}  // namespace cms::api
