#include <cms/api/verdict_view.hpp>
#include <cms/api/web.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

namespace {

auto contains(const std::string& haystack, std::string_view needle) -> bool {
  return haystack.find(needle) != std::string::npos;
}

auto render(const cms::api::Locale& locale, std::string_view task_id,
            const cms::api::VerdictView& view) -> cms::Result<std::string> {
  return cms::api::render_fragment(cms::api::verdict_fragment_source(view.is_pending),
                                   cms::api::verdict_fragment_model(locale, task_id, view));
}

}  // namespace

TEST_CASE("the pending verdict fragment keeps polling and says nothing has arrived", "[web][unit]") {
  const auto fragment = render(cms::api::choose_locale("th", ""), "everyone-task",
                               cms::api::VerdictView{});
  REQUIRE(fragment.has_value());
  CHECK(contains(*fragment, "<section class=\"mt-8\" id=\"verdict\""));
  CHECK(contains(*fragment, "hx-get=\"/tasks/everyone-task/verdict\""));
  CHECK(contains(*fragment, "hx-trigger=\"every 2s\""));
  CHECK(contains(*fragment, "hx-swap=\"outerHTML\""));
  CHECK(contains(*fragment, "ยังไม่มีผลการตรวจ"));
}

TEST_CASE("the finished verdict fragment shows the verdict and stops polling", "[web][unit]") {
  const auto fragment = render(cms::api::choose_locale("en", ""), "everyone-task",
                               cms::api::VerdictView{.is_pending = false, .verdict = "ok"});
  REQUIRE(fragment.has_value());
  CHECK(contains(*fragment, "<section class=\"mt-8\" id=\"verdict\""));
  CHECK(contains(*fragment, "Accepted"));
  CHECK_FALSE(contains(*fragment, "hx-get"));
}

TEST_CASE("an unrecognised stored verdict is reported as judged, never echoed", "[web][unit]") {
  const auto fragment = render(cms::api::choose_locale("en", ""), "everyone-task",
                               cms::api::VerdictView{.is_pending = false, .verdict = "<script>"});
  REQUIRE(fragment.has_value());
  CHECK(contains(*fragment, "Judged"));
  CHECK_FALSE(contains(*fragment, "<script>"));
}

TEST_CASE("the verdict path escapes the task id it interpolates", "[web][unit]") {
  const auto fragment = render(cms::api::choose_locale("en", ""), "\"><script>", cms::api::VerdictView{});
  REQUIRE(fragment.has_value());
  CHECK_FALSE(contains(*fragment, "\"><script>"));
  CHECK(contains(*fragment, "&quot;&gt;&lt;script&gt;"));
}
