#include <cms/db/connection.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <tuple>
#include <variant>

namespace cms::db {
namespace {

constexpr std::int64_t SECONDS_PER_DAY = 86'400;
constexpr std::int64_t SECONDS_PER_HOUR = 3'600;
constexpr std::int64_t MICROS_PER_SECOND = 1'000'000;

auto is_digit(char character) -> bool { return character >= '0' && character <= '9'; }

auto take_number(std::string_view text, std::size_t& position, std::size_t digits) -> std::int64_t {
  if (position + digits > text.size()) {
    throw ::sqlpp::exception{std::format("timestamp \"{}\" ends inside a number", text)};
  }
  auto value = std::int64_t{0};
  for (auto taken = std::size_t{0}; taken < digits; ++taken) {
    const auto character = text[position + taken];
    if (!is_digit(character)) {
      throw ::sqlpp::exception{std::format("timestamp \"{}\" is not a number", text)};
    }
    value = value * 10 + static_cast<std::int64_t>(character - '0');
  }
  position += digits;
  return value;
}

auto take_character(std::string_view text, std::size_t& position, char expected) -> void {
  if (position >= text.size() || text[position] != expected) {
    throw ::sqlpp::exception{std::format("timestamp \"{}\" is not ISO 8601", text)};
  }
  ++position;
}

auto take_date(std::string_view text, std::size_t& position) -> std::chrono::year_month_day {
  const auto year = std::chrono::year{static_cast<int>(take_number(text, position, 4))};
  take_character(text, position, '-');
  const auto month = std::chrono::month{static_cast<unsigned>(take_number(text, position, 2))};
  take_character(text, position, '-');
  const auto day = std::chrono::day{static_cast<unsigned>(take_number(text, position, 2))};
  const auto date = std::chrono::year_month_day{year, month, day};
  if (!date.ok()) {
    throw ::sqlpp::exception{std::format("timestamp \"{}\" names no real day", text)};
  }
  return date;
}

auto take_clock(std::string_view text, std::size_t& position)
    -> std::tuple<std::int64_t, std::int64_t, std::int64_t> {
  if (position >= text.size() || (text[position] != ' ' && text[position] != 'T')) {
    throw ::sqlpp::exception{std::format("timestamp \"{}\" is not ISO 8601", text)};
  }
  ++position;
  const auto hour = take_number(text, position, 2);
  take_character(text, position, ':');
  const auto minute = take_number(text, position, 2);
  take_character(text, position, ':');
  return {hour, minute, take_number(text, position, 2)};
}

auto take_fraction(std::string_view text, std::size_t& position) -> std::int64_t {
  auto micros = std::int64_t{0};
  if (position >= text.size() || text[position] != '.') {
    return micros;
  }
  ++position;
  auto digits = std::size_t{0};
  while (position < text.size() && digits < 6 && is_digit(text[position])) {
    micros = micros * 10 + static_cast<std::int64_t>(text[position] - '0');
    ++position;
    ++digits;
  }
  for (; digits < 6; ++digits) {
    micros *= 10;
  }
  while (position < text.size() && is_digit(text[position])) {
    ++position;
  }
  return micros;
}

auto take_offset(std::string_view text, std::size_t& position) -> std::int64_t {
  if (position < text.size() && text[position] == 'Z') {
    ++position;
    return 0;
  }
  if (position >= text.size() || (text[position] != '+' && text[position] != '-')) {
    return 0;
  }
  const auto is_behind_utc = text[position] == '-';
  ++position;
  auto offset = take_number(text, position, 2) * SECONDS_PER_HOUR;
  if (position < text.size() && text[position] == ':') {
    ++position;
    offset += take_number(text, position, 2) * 60;
    if (position < text.size() && text[position] == ':') {
      ++position;
      offset += take_number(text, position, 2);
    }
  }
  return is_behind_utc ? -offset : offset;
}

/// PostgreSQL prints a timestamptz as `2026-10-31 11:22:33.123456+07` under the ISO DateStyle.
auto parse_timestamp(std::string_view text) -> timestamp {
  auto position = std::size_t{0};
  const auto date = take_date(text, position);
  const auto [hour, minute, second] = take_clock(text, position);
  const auto micros = take_fraction(text, position);
  const auto offset = take_offset(text, position);
  if (position != text.size()) {
    throw ::sqlpp::exception{std::format("timestamp \"{}\" has trailing text", text)};
  }

  const auto seconds = std::chrono::sys_days{date}.time_since_epoch().count() * SECONDS_PER_DAY +
                       hour * SECONDS_PER_HOUR + minute * 60 + second - offset;
  return timestamp{std::chrono::duration_cast<timestamp::duration>(
                       std::chrono::seconds{seconds})} +
         timestamp::duration{micros};
}

auto format_timestamp(timestamp value) -> std::string {
  const auto micros = value.time_since_epoch().count();
  auto seconds = micros / MICROS_PER_SECOND;
  auto fraction = micros % MICROS_PER_SECOND;
  if (fraction < 0) {
    fraction += MICROS_PER_SECOND;
    --seconds;
  }
  auto days = seconds / SECONDS_PER_DAY;
  auto second_of_day = seconds % SECONDS_PER_DAY;
  if (second_of_day < 0) {
    second_of_day += SECONDS_PER_DAY;
    --days;
  }
  const auto date = std::chrono::year_month_day{std::chrono::sys_days{std::chrono::days{days}}};
  return std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:06}+00", static_cast<int>(date.year()),
                     static_cast<unsigned>(date.month()), static_cast<unsigned>(date.day()),
                     second_of_day / SECONDS_PER_HOUR,
                     (second_of_day % SECONDS_PER_HOUR) / 60, second_of_day % 60, fraction);
}

auto driver_text(const bound_value& value) -> std::optional<std::string> {
  return std::visit(
      [](const auto& held) -> std::optional<std::string> {
        using held_type = std::decay_t<decltype(held)>;
        if constexpr (std::is_same_v<held_type, std::nullptr_t>) {
          return std::nullopt;
        } else if constexpr (std::is_same_v<held_type, bool>) {
          return held ? "true" : "false";
        } else if constexpr (std::is_same_v<held_type, std::int64_t>) {
          return std::to_string(held);
        } else if constexpr (std::is_same_v<held_type, timestamp>) {
          return format_timestamp(held);
        } else {
          static_assert(std::is_same_v<held_type, std::string>,
                        "every bound value has to reach the driver as text or as NULL");
          return held;
        }
      },
      value);
}

auto require_value(const ::drogon::orm::Field& field) -> ::drogon::orm::Field {
  if (field.isNull()) {
    throw ::sqlpp::exception{std::format(
        "column \"{}\" came back NULL but the statement selects it as a required value",
        field.name())};
  }
  return field;
}

}  // namespace

result::result(::drogon::orm::Result rows) : rows_(std::move(rows)), row_count_(rows_.size()) {}

auto result::get_is_null(std::size_t field_index) const -> bool {
  return rows_[current_row_][field_index].isNull();
}

auto result::text(std::size_t field_index) const -> std::string_view {
  return require_value(rows_[current_row_][field_index]).as<std::string_view>();
}

auto result::integral(std::size_t field_index) const -> std::int64_t {
  return require_value(rows_[current_row_][field_index]).as<std::int64_t>();
}

auto result::boolean(std::size_t field_index) const -> bool {
  return require_value(rows_[current_row_][field_index]).as<bool>();
}

auto result::stamp(std::size_t field_index) const -> timestamp {
  return parse_timestamp(require_value(rows_[current_row_][field_index]).as<std::string_view>());
}

connection_handle::connection_handle(const std::shared_ptr<const connection_config>& config)
    : client_(config == nullptr ? nullptr : config->client) {}

auto connection_handle::client() const -> ::drogon::orm::DbClient& {
  if (client_ == nullptr) {
    throw ::sqlpp::exception{"this connection was never given a Drogon database client"};
  }
  return *client_;
}

auto connection_handle::ping_server() const noexcept -> bool {
  try {
    return !run(*this, serialized_statement{"SELECT 1", {}}).empty();
  } catch (const std::exception&) {
    return false;
  }
}

auto run(const connection_handle& handle, const serialized_statement& statement)
    -> ::drogon::orm::Result {
  auto binder = handle.client() << statement.sql;
  for (const auto& parameter : statement.parameters) {
    const auto text = driver_text(parameter);
    if (text.has_value()) {
      binder << *text;
    } else {
      binder << nullptr;
    }
  }

  // The driver reports a failed statement by throwing from exec(); a callback that never ran leaves no
  // result at all, and result::result would dereference it when it asks for the row count.
  auto received = std::optional<::drogon::orm::Result>{};
  binder << ::drogon::orm::Mode::Blocking;
  binder >> [&received](const ::drogon::orm::Result& result) { received = result; };
  binder.exec();
  if (!received.has_value()) {
    throw ::sqlpp::exception{
        std::format("statement \"{}\" returned no result from the driver", statement.sql)};
  }
  return std::move(*received);
}

}  // namespace cms::db
