#pragma once

#include <exception>
#include <string>
#include <string_view>

#include <sqlpp23/sqlpp23.h>

#include <cms/db/connection.hpp>
#include <cms/error.hpp>

namespace cms::db {

SQLPP_CREATE_NAME_TAG(field_advice);

/// @brief The one sentence a file-kind field's refusal carries, asked of the schema that enforces it.
///
/// Written once in `deploy/schema/fields.sql` as `file_field_advice()`, because the trigger raises it too. A layer
/// that composed its own sentence would be one more copy of a message that must not drift, and the sentence names
/// the field and the next step without echoing the value that was refused.
///
/// @param database The connection to ask; the function is immutable and reads no table.
/// @param field_key The field being refused, so the sentence names it.
/// @return The wording, or the reason the schema could not be asked.
[[nodiscard]] inline auto file_field_advice(cms::db::connection& database, std::string_view field_key)
    -> Result<std::string> {
  using advice_row = ::sqlpp::result_row_t<::sqlpp::field_spec_t<field_advice_t::_sqlpp_name_tag, ::sqlpp::text>>;
  try {
    auto rows = ::sqlpp::result_t<result, advice_row>{database.query_text(
        "SELECT file_field_advice($1) AS field_advice", {std::string{field_key}})};
    if (rows.empty()) {
      return std::unexpected(
          Error::make(ErrorCode::internal, "the schema returned no wording for a file field's refusal"));
    }
    return std::string{rows.front().field_advice};
  } catch (const std::exception& failure) {
    return std::unexpected(Error::make(ErrorCode::unavailable, failure.what()));
  }
}

}  // namespace cms::db
