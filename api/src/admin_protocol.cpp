#include <cms/api/admin_protocol.hpp>

#include <cstdint>
#include <flat_map>
#include <string>
#include <string_view>

#include <simdjson.h>

#include <cms/api/elevation.hpp>
#include <cms/error.hpp>
#include <cms/settings_snapshot.hpp>

namespace cms::api {
namespace {

/// The fields one request carries, split by the only two JSON types this protocol admits. A nested
/// object or array is refused rather than ignored, so a client cannot believe it sent something the
/// core quietly dropped.
struct Request {
  // Copied, not viewed: the DOM parser owns the buffer its views point into, and it is gone by the time
  // this leaves parse(). A request is a few hundred bytes, so the copy costs nothing that matters.
  std::flat_map<std::string, std::string, std::less<>> strings;
  std::flat_map<std::string, std::int64_t, std::less<>> numbers;
  bool is_valid{true};
};

/// Adds one field to the request. False means the value had a type this protocol does not carry, which
/// is a refusal rather than something to skip: a client must never believe it sent a fact the core
/// dropped.
auto read_field(Request& request, std::string_view key, simdjson::dom::element value) -> bool {
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

auto parse(const simdjson::padded_string& json) -> Request {
  auto request = Request{};
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

auto text_of(const Request& request, std::string_view name) -> std::string_view {
  const auto entry = request.strings.find(name);
  return entry == request.strings.end() ? std::string_view{} : entry->second;
}

auto number_of(const Request& request, std::string_view name) -> std::int64_t {
  const auto entry = request.numbers.find(name);
  return entry == request.numbers.end() ? 0 : entry->second;
}

auto escape(std::string_view text) -> std::string {
  auto quoted = std::string{};
  quoted.reserve(text.size());
  for (const auto character : text) {
    switch (character) {
      case '"':
        quoted += "\\\"";
        break;
      case '\\':
        quoted += "\\\\";
        break;
      case '\n':
        quoted += "\\n";
        break;
      case '\r':
        quoted += "\\r";
        break;
      case '\t':
        quoted += "\\t";
        break;
      default:
        quoted.push_back(character);
    }
  }
  return quoted;
}

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

auto require(const Request& request, std::string_view name) -> bool {
  return !text_of(request, name).empty();
}

auto answer_elevation_grant(const Request& request, const SettingsSnapshot& settings,
                            db::connection& database) -> std::string {
  for (const auto name : {"principal_id", "capability", "granted_by", "reason"}) {
    if (!require(request, name)) {
      return refused("an elevation needs " + std::string{name});
    }
  }

  const auto outcome = grant_elevation(
      database, settings,
      ElevationRequest{.principal_id = std::string{text_of(request, "principal_id")},
                       .capability = std::string{text_of(request, "capability")},
                       .granted_by = std::string{text_of(request, "granted_by")},
                       .reason = std::string{text_of(request, "reason")},
                       .minutes = number_of(request, "minutes")});
  if (!outcome) {
    return refused(outcome.error());
  }
  return accepted(",\"elevation_id\":\"" + outcome->id + "\",\"minutes\":" +
                  std::to_string(outcome->minutes) + ",\"live\":" +
                  (outcome->is_live ? "true" : "false"));
}

auto answer_elevation_approve(const Request& request, db::connection& database) -> std::string {
  for (const auto name : {"elevation_id", "approved_by"}) {
    if (!require(request, name)) {
      return refused("an approval needs " + std::string{name});
    }
  }
  const auto outcome = approve_elevation(database, text_of(request, "elevation_id"),
                                         text_of(request, "approved_by"));
  if (!outcome) {
    return refused(outcome.error());
  }
  return accepted(",\"live\":true");
}

}  // namespace

auto dispatch_line(std::string_view request, const SettingsSnapshot& settings,
                   db::connection& database) -> std::string {
  // The padded copy owns the bytes every field view points into, so it outlives the parsed request.
  const auto json = simdjson::padded_string{request};
  const auto fields = parse(json);
  if (!fields.is_valid) {
    return refused("the request is not one flat JSON object of strings and integers");
  }

  const auto action = text_of(fields, "action");
  if (action == "ping") {
    return accepted(",\"settings_version\":" + std::to_string(settings.version));
  }
  if (action == "status") {
    return accepted(",\"settings_version\":" + std::to_string(settings.version));
  }
  if (action == "elevation_grant") {
    return answer_elevation_grant(fields, settings, database);
  }
  if (action == "elevation_approve") {
    return answer_elevation_approve(fields, database);
  }
  return refused("unknown action: " + std::string{action});
}

}  // namespace cms::api
