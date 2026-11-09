#pragma once

#include <cstdint>
#include <flat_map>
#include <functional>
#include <string>
#include <string_view>

#include <cms/api/actor.hpp>
#include <cms/db/connection.hpp>
#include <cms/error.hpp>
#include <cms/settings_snapshot.hpp>

namespace cms::api::detail {

// Copied, not viewed: the simdjson buffer dies with the parser, so a view would dangle here.
struct Request {
  std::flat_map<std::string, std::string, std::less<>> strings;
  std::flat_map<std::string, std::int64_t, std::less<>> numbers;
  bool is_valid{true};
};

inline auto text_of(const Request& request, std::string_view name) -> std::string_view {
  const auto entry = request.strings.find(name);
  return entry == request.strings.end() ? std::string_view{} : entry->second;
}

inline auto number_of(const Request& request, std::string_view name) -> std::int64_t {
  const auto entry = request.numbers.find(name);
  return entry == request.numbers.end() ? 0 : entry->second;
}

inline auto require(const Request& request, std::string_view name) -> bool {
  return !text_of(request, name).empty();
}

inline auto present(const Request& request, std::string_view name) -> bool {
  return request.strings.find(name) != request.strings.end();
}

inline auto escape(std::string_view text) -> std::string {
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

auto accepted(std::string_view body) -> std::string;
auto refused(std::string_view reason) -> std::string;
auto refused(const Error& error) -> std::string;

/// The actor field the request carries, or empty when it names no actor.
auto names_caller(const Request& request) -> std::string_view;

auto answer_create_learner(const Request& request, const SettingsSnapshot& settings,
                           db::connection& database, const Actor& caller) -> std::string;
auto answer_create_admin(const Request& request, const SettingsSnapshot& settings,
                         db::connection& database, const Actor& caller) -> std::string;
auto answer_approve_learner(const Request& request, db::connection& database,
                            const Actor& caller) -> std::string;
auto answer_issue_verification(const Request& request, const SettingsSnapshot& settings,
                               db::connection& database, const Actor& caller) -> std::string;
auto answer_revoke_admin(const Request& request, db::connection& database, const Actor& caller)
    -> std::string;

}  // namespace cms::api::detail
