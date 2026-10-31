#include <cms/api/mailer.hpp>
#include <cms/api/settings_defaults.hpp>

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>

namespace {

auto empty_snapshot() -> cms::SettingsSnapshot {
  return cms::SettingsSnapshot{1, {}};
}

}  // namespace

TEST_CASE("mail is disabled by default and a disabled mailer is not an error", "[mail]") {
  const auto snapshot = empty_snapshot();

  const auto sent = cms::api::send_mail(snapshot, {"student@example.org", "Verify", "body"});
  REQUIRE(sent.has_value());
  CHECK(*sent == false);
}

TEST_CASE("enabling mail without a host is refused rather than attempted", "[mail]") {
  auto snapshot = empty_snapshot();
  snapshot.values.emplace("mail.enabled", true);

  const auto sent = cms::api::send_mail(snapshot, {"student@example.org", "Verify", "body"});
  REQUIRE(!sent.has_value());
  CHECK(sent.error().message.find("mail.host") != std::string::npos);
}

TEST_CASE("a setting falls back to the declared default when the row is absent", "[settings]") {
  const auto snapshot = empty_snapshot();

  CHECK(cms::api::setting_bool(snapshot, "site.registration_open") == false);
  CHECK(cms::api::setting_bool(snapshot, "site.registration_requires_approval") == true);
  CHECK(cms::api::setting_text(snapshot, "mail.host").empty());
  CHECK(cms::api::setting_int(snapshot, "mail.port") == 587);
}

TEST_CASE("a stored row wins over the declared default", "[settings]") {
  auto snapshot = empty_snapshot();
  snapshot.values.emplace("site.registration_open", true);

  CHECK(cms::api::setting_bool(snapshot, "site.registration_open") == true);
}
