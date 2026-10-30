#include <string_view>
#include <type_traits>

#include <catch2/catch_test_macros.hpp>

#include <sqlpp23/sqlpp23.h>

#include <cms/db/settings_query.hpp>
#include <cms/db/tables.hpp>

namespace {

using SettingsKey = std::remove_cvref_t<decltype(cms::db::settings.key)>;
using SettingsValueText = std::remove_cvref_t<decltype(cms::db::settings.valueText)>;
using SessionsExpiresAt = std::remove_cvref_t<decltype(cms::db::sessions.expiresAt)>;

static_assert(sqlpp::is_column_v<SettingsKey>, "the settings table needs a key column");
static_assert(std::is_same_v<sqlpp::data_type_of_t<SettingsKey>, sqlpp::text>);
static_assert(std::is_same_v<sqlpp::data_type_of_t<SettingsValueText>, sqlpp::text>);
static_assert(sqlpp::is_column_v<SessionsExpiresAt>, "the sessions table needs an expiry column");
static_assert(std::is_same_v<sqlpp::data_type_of_t<SessionsExpiresAt>, sqlpp::timestamp>);

static_assert(std::is_invocable_v<decltype(&cms::db::settings_snapshot_query)>);
static_assert(std::is_invocable_v<decltype(&cms::db::live_session_query)>);

}  // namespace

TEST_CASE("columns keep the SQL names the plan gives them", "[db]") {
  REQUIRE(std::string_view{sqlpp::name_tag_of_t<SettingsKey>::name} == "key");
  REQUIRE(std::string_view{sqlpp::name_tag_of_t<SettingsValueText>::name} == "value_text");
  REQUIRE(std::string_view{sqlpp::name_tag_of_t<SessionsExpiresAt>::name} == "expires_at");
}
