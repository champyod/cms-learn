#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <drogon/orm/DbClient.h>
#include <drogon/orm/Result.h>
#include <sqlpp23/sqlpp23.h>

namespace cms::db {

/// @brief The value type sqlpp23's result row carries for a `timestamp` column.
using timestamp = std::chrono::time_point<std::chrono::system_clock, std::chrono::microseconds>;

/// @brief Renders a timestamp as PostgreSQL text with an explicit UTC offset.
[[nodiscard]] auto format_timestamp(timestamp value) -> std::string;

/// @brief One positional parameter, still typed: the driver turns it into text only when it binds it.
using bound_value = std::variant<std::nullptr_t, bool, std::int64_t, std::string, timestamp>;

/// @brief A statement rendered to PostgreSQL text plus its parameters, in binding order.
struct serialized_statement {
  std::string sql;
  std::vector<bound_value> parameters;
};

template <typename T>
[[nodiscard]] auto make_bound_value(const T& value) -> bound_value {
  if constexpr (requires { value.has_value(); }) {
    if (!value.has_value()) {
      return nullptr;
    }
    return make_bound_value(*value);
  } else {
    static_assert(std::is_same_v<T, std::string> || std::is_same_v<T, std::int64_t> ||
                      std::is_same_v<T, bool> || std::is_same_v<T, timestamp>,
                  "this connector binds text, integral, boolean and timestamp parameters only");
    return bound_value{value};
  }
}

/// Collects the parameters of one statement while sqlpp23 walks it, so text and values share an order.
template <typename Bindings>
class context {
 public:
  explicit context(const Bindings& bindings) : bindings_(bindings) {}

  auto escape(std::string_view text) const -> std::string {
    auto escaped = std::string{};
    escaped.reserve(text.size());
    for (const auto character : text) {
      escaped.push_back(character);
      if (character == '\'') {
        escaped.push_back('\'');
      }
    }
    return escaped;
  }

  template <typename Parameter>
  auto bind(const Parameter&) -> std::string {
    const auto& value = static_cast<const typename Parameter::_instance_t&>(bindings_)();
    parameters_.push_back(make_bound_value(value));
    return "$" + std::to_string(parameters_.size());
  }

  auto bound_parameters() -> std::vector<bound_value> { return std::move(parameters_); }

 private:
  const Bindings& bindings_;
  std::vector<bound_value> parameters_;
};

template <typename Bindings, typename DataType, typename NameTag>
[[nodiscard]] auto to_sql_string(context<Bindings>& target,
                                 const ::sqlpp::parameter_t<DataType, NameTag>& parameter) -> std::string {
  return target.bind(parameter);
}

/// @brief Renders a timestamp literal with its offset.
///
/// Without this the serializer writes a bare wall clock and Postgres reads it in the session timezone,
/// which moves the instant by the server's UTC offset — the bug that made a session expire before it
/// was created.
template <typename Bindings>
auto to_sql_string(context<Bindings>&, const timestamp& value) -> std::string {
  return "'" + format_timestamp(value) + "'";
}

/// Every parameter of a statement, addressed by the name its statement declared it with.
template <typename Statement>
auto bindings_for(const Statement&) -> ::sqlpp::make_parameter_list_t<Statement> {
  return {};
}

/// The rows of one select, decoded into the result row type the statement declares.
class result {
 public:
  result() = default;
  explicit result(::drogon::orm::Result rows);

  template <typename Row>
  void next(Row& row) {
    auto bridge = ::sqlpp::detail::result_row_bridge{};
    if (row_index_ >= row_count_) {
      bridge.invalidate(row);
      return;
    }
    current_row_ = row_index_;
    ++row_index_;
    bridge.read_fields(row, *this);
    bridge.validate(row);
  }

  [[nodiscard]] auto get_is_null(std::size_t field_index) const -> bool;
  [[nodiscard]] auto text(std::size_t field_index) const -> std::string_view;
  [[nodiscard]] auto integral(std::size_t field_index) const -> std::int64_t;
  [[nodiscard]] auto boolean(std::size_t field_index) const -> bool;
  [[nodiscard]] auto stamp(std::size_t field_index) const -> timestamp;

 private:
  ::drogon::orm::Result rows_{nullptr};
  std::size_t row_count_{0};
  std::size_t row_index_{0};
  std::size_t current_row_{0};
};

inline auto read_field(result& rows, std::size_t field_index, std::string_view& value) -> void {
  value = rows.text(field_index);
}

inline auto read_field(result& rows, std::size_t field_index, std::int64_t& value) -> void {
  value = rows.integral(field_index);
}

inline auto read_field(result& rows, std::size_t field_index, bool& value) -> void {
  value = rows.boolean(field_index);
}

inline auto read_field(result& rows, std::size_t field_index, timestamp& value) -> void {
  value = rows.stamp(field_index);
}

/// @brief The pool a connection borrows from: the only thing it needs from Drogon.
struct connection_config {
  std::shared_ptr<::drogon::orm::DbClient> client;
};

/// @brief Owns the pool Drogon handed out and is the only thing sqlpp23 reaches the server through.
class connection_handle {
 public:
  connection_handle() = default;
  explicit connection_handle(const std::shared_ptr<const connection_config>& config);

  /// @brief Whether a usable client was supplied.
  [[nodiscard]] auto is_connected() const noexcept -> bool { return client_ != nullptr; }
  /// @brief Asks the server for a trivial query and reports whether it answered.
  [[nodiscard]] auto ping_server() const noexcept -> bool;
  /// @brief The pool, or an exception when none was configured.
  [[nodiscard]] auto client() const -> ::drogon::orm::DbClient&;

 private:
  std::shared_ptr<::drogon::orm::DbClient> client_;
};

/// @brief Runs one statement and hands back the driver's rows.
/// @param handle The connection to run on.
/// @param statement The rendered SQL and its parameters, in order.
/// @return The driver's rows, which outlive every view into them.
[[nodiscard]] auto run(const connection_handle& handle, const serialized_statement& statement)
    -> ::drogon::orm::Result;

/// @brief The sqlpp23 connection this project supplies: CRTP over the handle, not a virtual interface.
class connection_base : public ::sqlpp::connection {
 public:
  using _config_t = connection_config;
  using _config_ptr_t = std::shared_ptr<const _config_t>;
  using _handle_t = connection_handle;

  connection_base() = default;
  explicit connection_base(_handle_t handle) : _handle(std::move(handle)) {}

  template <typename Statement>
  auto operator()(Statement&& statement) {
    return ::sqlpp::statement_handler_t{}.run(std::forward<Statement>(statement), *this);
  }

  /// Runs a select whose parameters the caller filled by name in `bindings_for(statement)`.
  template <typename Statement, typename Bindings>
  [[nodiscard]] auto select(Statement&& statement, const Bindings& bindings)
      -> ::sqlpp::result_t<result, ::sqlpp::get_result_row_t<std::decay_t<Statement>>> {
    return ::sqlpp::result_t<result, ::sqlpp::get_result_row_t<std::decay_t<Statement>>>{
        rows_of(std::forward<Statement>(statement), bindings)};
  }

  /// Synchronous because sqlpp23's interface is: a caller on an event-loop thread must use a worker.
  template <typename Statement>
  auto _select(Statement&& statement) -> result {
    static_assert(::sqlpp::parameters_of_t<std::decay_t<Statement>>::empty(),
                  "a statement with parameters has to be run through select(statement, bindings)");
    return rows_of(std::forward<Statement>(statement), bindings_for(statement));
  }

  template <typename Statement, typename Bindings>
  [[nodiscard]] auto execute(Statement&& statement, const Bindings& bindings) -> std::size_t {
    return run(_handle, serialize(statement, bindings)).affectedRows();
  }

  template <typename Statement>
  auto _execute(Statement&& statement) -> std::size_t {
    static_assert(::sqlpp::parameters_of_t<std::decay_t<Statement>>::empty(),
                  "a statement with parameters has to be run through execute(statement, bindings)");
    return execute(std::forward<Statement>(statement), bindings_for(statement));
  }

  /// @brief Runs an insert. sqlpp23 0.70 defines a returning clause but wires it into no statement, so
  /// a write reports affected rows and the caller supplies any generated identity.
  template <typename Statement>
  auto _insert(Statement&& statement) -> std::size_t {
    static_assert(::sqlpp::parameters_of_t<std::decay_t<Statement>>::empty(),
                  "a statement with parameters has to be run through insert(statement, bindings)");
    return execute(std::forward<Statement>(statement), bindings_for(statement));
  }

  /// @brief Runs an update; the same shape as an insert because neither returns rows here.
  template <typename Statement>
  auto _update(Statement&& statement) -> std::size_t {
    static_assert(::sqlpp::parameters_of_t<std::decay_t<Statement>>::empty(),
                  "a statement with parameters has to be run through update(statement, bindings)");
    return execute(std::forward<Statement>(statement), bindings_for(statement));
  }

  /// @brief Runs a delete and reports how many rows went.
  template <typename Statement>
  auto _delete_from(Statement&& statement) -> std::size_t {
    static_assert(::sqlpp::parameters_of_t<std::decay_t<Statement>>::empty(),
                  "a statement with parameters has to be run through delete_from(statement, bindings)");
    return execute(std::forward<Statement>(statement), bindings_for(statement));
  }

  template <typename Statement, typename Bindings>
  auto insert(Statement&& statement, const Bindings& bindings) -> std::size_t {
    return execute(std::forward<Statement>(statement), bindings);
  }

  template <typename Statement, typename Bindings>
  auto update(Statement&& statement, const Bindings& bindings) -> std::size_t {
    return execute(std::forward<Statement>(statement), bindings);
  }

 protected:
  _handle_t _handle{};

 private:
  template <typename Statement, typename Bindings>
  static auto serialize(const Statement& statement, const Bindings& bindings) -> serialized_statement {
    auto target = context<Bindings>{bindings};
    auto sql = ::sqlpp::to_sql_string(target, statement);
    return serialized_statement{std::move(sql), target.bound_parameters()};
  }

  template <typename Statement, typename Bindings>
  auto rows_of(const Statement& statement, const Bindings& bindings) -> result {
    return result{run(_handle, serialize(statement, bindings))};
  }
};

using connection = ::sqlpp::normal_connection<connection_base>;

}  // namespace cms::db
