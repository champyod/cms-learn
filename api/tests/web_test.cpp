#include <cms/api/web.hpp>

#include <catch2/catch_test_macros.hpp>

#include <unistd.h>

#include <filesystem>
#include <fstream>
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

auto home_page(const cms::api::Locale& locale) -> cms::Result<std::string> {
  return cms::api::render_page(template_root(), "home.html",
                               cms::api::home_model(locale, sample_curriculum(), "CMS Learn"));
}

auto sample_task() -> cms::api::TaskPage {
  auto task = cms::api::TaskPage{};
  task.id = "range-sum-query";
  task.title_en = "Range Sum Queries";
  task.title_th = "ผลรวมของช่วงข้อมูล";
  task.time_limit_ms = 1500;
  task.memory_limit_mb = 200;
  task.statement_en = "# Range Sum Queries\n\nAnswer each query in order.";
  task.statement_th = "# ผลรวมของช่วงข้อมูล\n\nจงตอบผลรวมของแต่ละช่วง";
  task.example_input = "3 2\n1 2 3\n1 2\n2 3\n";
  task.example_output = "3\n5\n";
  return task;
}

auto task_page(const cms::api::Locale& locale) -> cms::Result<std::string> {
  return cms::api::render_page(template_root(), "task.html",
                               cms::api::task_model(locale, sample_task(), "CMS Learn"));
}

/// A published tree with one asset, and one file beside it that no request may reach.
class ScratchTree {
 public:
  ScratchTree() {
    root_ = std::filesystem::temp_directory_path() /
            ("cms-web-static-" + std::to_string(::getpid()));
    std::filesystem::create_directories(assets());
    auto asset = std::ofstream{assets() / "app.css"};
    asset << "body{}";
    auto beside = std::ofstream{root_ / "secret.txt"};
    beside << "not published";
  }

  ~ScratchTree() {
    auto error = std::error_code{};
    std::filesystem::remove_all(root_, error);
  }

  ScratchTree(const ScratchTree&) = delete;
  ScratchTree& operator=(const ScratchTree&) = delete;
  ScratchTree(ScratchTree&&) = delete;
  ScratchTree& operator=(ScratchTree&&) = delete;

  [[nodiscard]] std::filesystem::path assets() const { return root_ / "assets"; }
  [[nodiscard]] std::filesystem::path root() const { return root_; }

 private:
  std::filesystem::path root_;
};

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
  const auto page = home_page(cms::api::choose_locale("th", ""));
  REQUIRE(page.has_value());

  CHECK(contains(*page, "<html lang=\"th\" data-theme=\"cms\">"));
  CHECK(contains(*page, "เส้นทางแบบ USACO"));
  CHECK(contains(*page, "ทองแดง"));
  CHECK(contains(*page, "prefix-sums"));
  CHECK(contains(*page, "1100"));
  CHECK(contains(*page, "?lang=en"));
}

TEST_CASE("a stage with no Thai title renders its English one, not an empty heading",
          "[web][unit]") {
  // The Thai stage title is empty in the data, and the page must show the English label rather than
  // `<h3></h3>`.
  const auto page = home_page(cms::api::choose_locale("th", ""));
  REQUIRE(page.has_value());
  CHECK(contains(*page, "Silver"));
  CHECK(!contains(*page, "<h3></h3>"));
}

TEST_CASE("the English page carries the English navigation", "[web][unit]") {
  const auto page = home_page(cms::api::choose_locale("en", ""));
  REQUIRE(page.has_value());
  CHECK(contains(*page, "<html lang=\"en\" data-theme=\"cms\">"));
  CHECK(contains(*page, "Curriculum"));
  CHECK(contains(*page, "USACO ladder"));
  CHECK(contains(*page, "?lang=th"));
}

TEST_CASE("a page whose template is missing is an error, not an empty document", "[web][unit]") {
  const auto page = cms::api::render_page(
      "/nonexistent/templates", "home.html",
      cms::api::home_model(cms::api::choose_locale("th", ""), sample_curriculum(), "CMS Learn"));
  REQUIRE(!page.has_value());
}

TEST_CASE("an asset under the published root is served", "[web][unit]") {
  const auto tree = ScratchTree{};
  const auto file = cms::api::static_file(tree.assets().string(), "app.css");

  REQUIRE(file.has_value());
  CHECK(std::filesystem::equivalent(*file, tree.assets() / "app.css"));
}

TEST_CASE("a path that resolves outside the published root is refused", "[web][unit]") {
  const auto tree = ScratchTree{};
  const auto root = tree.assets().string();
  std::filesystem::create_symlink(tree.root() / "secret.txt", tree.assets() / "link.txt");

  CHECK(!cms::api::static_file(root, "../secret.txt").has_value());
  CHECK(!cms::api::static_file(root, "/etc/hostname").has_value());
  CHECK(!cms::api::static_file(root, "link.txt").has_value());
  CHECK(!cms::api::static_file(root, "").has_value());
  CHECK(!cms::api::static_file(root, "absent.css").has_value());
}

TEST_CASE("the task page renders its title and first example", "[web][unit]") {
  const auto page = task_page(cms::api::choose_locale("th", ""));
  REQUIRE(page.has_value());

  CHECK(contains(*page, "ผลรวมของช่วงข้อมูล"));
  CHECK(contains(*page, "1500"));
  CHECK(contains(*page, "200"));
  CHECK(contains(*page, "1 2 3"));
  CHECK(contains(*page, "3\n5"));
}

TEST_CASE("the task page follows the requested locale", "[web][unit]") {
  const auto page = task_page(cms::api::choose_locale("en", ""));
  REQUIRE(page.has_value());

  CHECK(contains(*page, "<html lang=\"en\" data-theme=\"cms\">"));
  CHECK(contains(*page, "Range Sum Queries"));
  CHECK(contains(*page, "Answer each query in order."));
  CHECK(contains(*page, "?lang=th"));
}

TEST_CASE("a missing statement locale falls back to the other one", "[web][unit]") {
  auto task = sample_task();
  task.statement_th = "";
  const auto page = cms::api::render_page(
      template_root(), "task.html",
      cms::api::task_model(cms::api::choose_locale("th", ""), task, "CMS Learn"));
  REQUIRE(page.has_value());

  CHECK(contains(*page, "Answer each query in order."));
  CHECK(!contains(*page, "<article class=\"prose mt-6 max-w-none\"><div></div></article>"));
}

TEST_CASE("the task model carries no reference solution and no starter source", "[web][unit]") {
  const auto model = cms::api::task_model(cms::api::choose_locale("th", ""), sample_task(),
                                          "CMS Learn");

  CHECK(!model.contains("solution"));
  CHECK(!model["task"].contains("solution"));
  CHECK(model["task"].contains("starter_source"));
  CHECK(model["task"]["starter_source"] == "");
  CHECK(model["submit_action"] == "/tasks/range-sum-query/submit");
  CHECK(model["verdict_url"] == "/tasks/range-sum-query/verdict");
}

TEST_CASE("a statement carrying markup is shown as text, never injected into the page",
          "[web][unit]") {
  auto task = sample_task();
  task.statement_en = "<script>alert(1)</script>";
  const auto page = cms::api::render_page(
      template_root(), "task.html",
      cms::api::task_model(cms::api::choose_locale("en", ""), task, "CMS Learn"));
  REQUIRE(page.has_value());

  CHECK(contains(*page, "&lt;script&gt;alert(1)&lt;/script&gt;"));
  CHECK(!contains(*page, "<script>alert(1)</script>"));
}
