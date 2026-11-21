#include <cms/api/auth_view.hpp>
#include <cms/api/settings_page.hpp>
#include <cms/api/web.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace {

auto template_root() -> std::string { return CMS_TEMPLATE_DIR; }

auto contains(const std::string& haystack, std::string_view needle) -> bool {
  return haystack.find(needle) != std::string::npos;
}

/// The one `<input>` tag that carries @p needle, so a test asserts about the field this page renders rather
/// than about every value attribute on the page.
auto tag_of(const std::string& page, std::string_view needle) -> std::string {
  const auto at = page.find(needle);
  if (at == std::string::npos) {
    return {};
  }
  const auto start = page.rfind('<', at);
  const auto end = page.find('>', at);
  if (start == std::string::npos || end == std::string::npos) {
    return {};
  }
  return page.substr(start, end - start + 1);
}

auto render(std::string_view template_name, const nlohmann::json& model)
    -> cms::Result<std::string> {
  return cms::api::render_page(template_root(), template_name, model);
}

auto sample_settings() -> std::vector<cms::api::VisibleSetting> {
  return {
      {.key = "site.name", .kind = "text", .tier = "public", .value = "CMS Learn", .reload = "hot"},
      // Catalogued and read by nothing, so the page must not offer it (ADR 0054 §6).
      {.key = "site.maintenance_mode", .kind = "bool", .tier = "operator", .value = "false", .reload = "hot"},
      {.key = "mail.host", .kind = "text", .tier = "operator", .value = "smtp.example.org", .reload = "hot"},
      {.key = "mail.password", .kind = "text", .tier = "secret", .value = "<redacted>", .reload = "restart"},
  };
}

}  // namespace

TEST_CASE("the sign-in form carries neither a password nor an email value", "[web][unit]") {
  const auto page = render("signin.html", cms::api::signin_model(cms::api::choose_locale("th", ""),
                                                                 "CMS Learn", ""));
  REQUIRE(page.has_value());
  CHECK(contains(*page, "<html lang=\"th\" data-theme=\"cms\">"));
  CHECK(contains(*page, "เข้าสู่ระบบ"));

  const auto password = tag_of(*page, "name=\"password\"");
  REQUIRE_FALSE(password.empty());
  CHECK(contains(password, "type=\"password\""));
  CHECK_FALSE(contains(password, "value="));

  const auto email = tag_of(*page, "name=\"email\"");
  REQUIRE_FALSE(email.empty());
  CHECK_FALSE(contains(email, "value="));
}

TEST_CASE("the sign-up form carries no password value", "[web][unit]") {
  const auto page = render("signup.html", cms::api::signup_model(cms::api::choose_locale("en", ""),
                                                                 "CMS Learn", ""));
  REQUIRE(page.has_value());
  CHECK(contains(*page, "<html lang=\"en\" data-theme=\"cms\">"));
  CHECK(contains(*page, "Sign up"));

  const auto password = tag_of(*page, "name=\"password\"");
  REQUIRE_FALSE(password.empty());
  CHECK(contains(password, "type=\"password\""));
  CHECK_FALSE(contains(password, "value="));
}

TEST_CASE("the sign-in notice is a canned phrase in the page's own language", "[web][unit]") {
  const auto thai = cms::api::choose_locale("th", "");
  const auto english = cms::api::choose_locale("en", "");

  CHECK(cms::api::signin_notice(thai, cms::api::SigninNotice::none).empty());
  CHECK(contains(cms::api::signin_notice(thai, cms::api::SigninNotice::approval_pending), "ผู้ดูแล"));
  CHECK(contains(cms::api::signin_notice(english, cms::api::SigninNotice::approval_pending),
                 "approve"));
}

TEST_CASE("the settings model renders one group and marks every secret value sealed", "[web][unit]") {
  const auto locale = cms::api::choose_locale("th", "");
  const auto model = cms::api::settings_model(locale, "CMS Learn", sample_settings(), "");
  CHECK(model["active_group"] == "site");
  REQUIRE(model["groups"].size() == 2);
  CHECK(model["groups"][0]["key"] == "site");
  CHECK(model["groups"][1]["key"] == "mail");
  // Only the offered ones: site.maintenance_mode is catalogued and unread, so it is not in this group.
  REQUIRE(model["settings"].size() == 1);
  CHECK(model["settings"][0]["key"] == "site.name");

  const auto mail = cms::api::settings_model(locale, "CMS Learn", sample_settings(), "mail");
  CHECK(mail["active_group"] == "mail");
  REQUIRE(mail["settings"].size() == 2);
  CHECK(mail["settings"][0]["key"] == "mail.host");
  CHECK(mail["settings"][1]["tier"] == "secret");
  // A secret is write-only: the page says it is stored and sealed, and the value itself never arrives here.
  CHECK(mail["settings"][1]["value"] == "sealed");
}

TEST_CASE("the settings model offers only what a service reads, and states what a change costs", "[web][unit]") {
  const auto locale = cms::api::choose_locale("en", "");
  const auto model = cms::api::settings_model(locale, "CMS Learn", sample_settings(), "site");
  // site.name is read by the page itself; site.tagline is catalogued and read by nothing, so it is not offered (ADR 0054 §6).
  REQUIRE(model["settings"].size() == 1);
  CHECK(model["settings"][0]["key"] == "site.name");
  CHECK(model["settings"][0]["scope"] == "site");
  CHECK(model["settings"][0]["permission"] == "settings:write");
  CHECK(model["settings"][0]["effect"] == "takes effect immediately");
  CHECK(model["settings"][0]["sealed"] == false);
  CHECK(model["counts"]["catalogued"] == 4);
  CHECK(model["counts"]["offered"] == 3);

  const auto mail = cms::api::settings_model(locale, "CMS Learn", sample_settings(), "mail");
  REQUIRE(mail["settings"].size() == 2);
  CHECK(mail["settings"][1]["sealed"] == true);
  CHECK(mail["settings"][1]["scope"] == "mail");
  CHECK(mail["settings"][1]["permission"] == "settings:secret_write");
  CHECK(mail["settings"][1]["effect"] == "takes effect after the api service restarts; the running service keeps the old value until then");
}

TEST_CASE("an unknown group falls back to the first group", "[web][unit]") {
  const auto model = cms::api::settings_model(cms::api::choose_locale("en", ""), "CMS Learn",
                                              sample_settings(), "nope");
  CHECK(model["active_group"] == "site");
}

TEST_CASE("the settings page renders a secret as an empty password field", "[web][unit]") {
  const auto mail = cms::api::settings_model(cms::api::choose_locale("en", ""), "CMS Learn",
                                             sample_settings(), "mail");
  const auto page = render("settings.html", mail);
  REQUIRE(page.has_value());
  CHECK(contains(*page, "smtp.example.org"));

  const auto password = tag_of(*page, "name=\"mail.password\"");
  REQUIRE_FALSE(password.empty());
  CHECK(contains(password, "type=\"password\""));
  CHECK_FALSE(contains(password, "value="));
  CHECK_FALSE(contains(*page, "<redacted>"));
}
