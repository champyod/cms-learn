#include <cms/api/web.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace {

auto template_root() -> std::string { return CMS_TEMPLATE_DIR; }

/// Two routes, one of which is deliberately missing its Thai stage title: that absence is the plan's
/// falsification, and it has to be visible in the rendered page rather than read from the data.
auto sample_curriculum() -> std::vector<cms::bank::Track> {
  auto stage = cms::bank::Stage{};
  stage.id = "st-silver";
  stage.title_en = "Silver";
  stage.title_th = "";
  stage.difficulty_min = 1100;
  stage.difficulty_max = 1500;
  stage.tags = {"prefix-sums", "binary-search"};

  auto translated = cms::bank::Stage{};
  translated.id = "st-bronze";
  translated.title_en = "Bronze";
  translated.title_th = "ทองแดง";
  translated.difficulty_min = 800;
  translated.difficulty_max = 1200;
  translated.tags = {"io"};

  auto track = cms::bank::Track{};
  track.id = "t-usaco";
  track.title_en = "USACO ladder";
  track.title_th = "เส้นทางแบบ USACO";
  track.description_en = "Four divisions";
  track.description_th = "สี่ระดับ";
  track.stages = {stage, translated};

  return {track};
}

auto contains(const std::string& haystack, std::string_view needle) -> bool {
  return haystack.find(needle) != std::string::npos;
}

}  // namespace

TEST_CASE("a request without a language gets the site's own", "[web][unit]") {
  CHECK(cms::api::choose_locale("", "").code == "th");
  CHECK(cms::api::choose_locale("en", "").code == "en");
  CHECK(cms::api::choose_locale("en", "th-TH").code == "en");
  CHECK(cms::api::choose_locale("", "en-GB,en;q=0.9").code == "en");
  CHECK(cms::api::choose_locale("", "th-TH,en;q=0.8").code == "th");
  CHECK(cms::api::choose_locale("", "").other == "en");
  CHECK(cms::api::choose_locale("en", "").other == "th");
}

TEST_CASE("a missing translation falls back to the other language", "[web][unit]") {
  const auto thai = cms::api::choose_locale("th", "");
  const auto english = cms::api::choose_locale("en", "");

  CHECK(cms::api::label(thai, "Silver", "เงิน") == "เงิน");
  CHECK(cms::api::label(thai, "Silver", "") == "Silver");
  CHECK(cms::api::label(english, "Silver", "เงิน") == "Silver");
  CHECK(cms::api::label(english, "", "เงิน") == "เงิน");
}

TEST_CASE("the page renders every route and its stages", "[web][unit]") {
  const auto page = cms::api::render_home(template_root(), cms::api::choose_locale("th", ""),
                                          sample_curriculum(), "CMS Learn");
  REQUIRE(page.has_value());

  CHECK(contains(*page, "<html lang=\"th\">"));
  CHECK(contains(*page, "เส้นทางแบบ USACO"));
  CHECK(contains(*page, "ทองแดง"));
  CHECK(contains(*page, "prefix-sums"));
  CHECK(contains(*page, "1100"));
  CHECK(contains(*page, "?lang=en"));
}

TEST_CASE("a stage with no Thai title renders its English one, not an empty heading",
          "[web][unit]") {
  // The plan's E6.1 falsification: the Thai stage title is empty in the data, and the page must show the
  // English label rather than `<h3></h3>`.
  const auto page = cms::api::render_home(template_root(), cms::api::choose_locale("th", ""),
                                          sample_curriculum(), "CMS Learn");
  REQUIRE(page.has_value());
  CHECK(contains(*page, "Silver"));
  CHECK(!contains(*page, "<h3></h3>"));
}

TEST_CASE("the English page carries the English navigation", "[web][unit]") {
  const auto page = cms::api::render_home(template_root(), cms::api::choose_locale("en", ""),
                                          sample_curriculum(), "CMS Learn");
  REQUIRE(page.has_value());
  CHECK(contains(*page, "<html lang=\"en\">"));
  CHECK(contains(*page, "Curriculum"));
  CHECK(contains(*page, "USACO ladder"));
  CHECK(contains(*page, "?lang=th"));
}

TEST_CASE("a page whose template is missing is an error, not an empty document", "[web][unit]") {
  const auto page = cms::api::render_home("/nonexistent/templates", cms::api::choose_locale("th", ""),
                                          sample_curriculum(), "CMS Learn");
  REQUIRE(!page.has_value());
}
