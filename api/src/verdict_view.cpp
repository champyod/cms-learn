#include <cms/api/verdict_view.hpp>

#include <string>
#include <string_view>

namespace cms::api {
namespace {

constexpr std::string_view PENDING_FRAGMENT =
    R"(<section class="mt-8" id="verdict" hx-get="{{ verdict_url }}" hx-trigger="every 2s" hx-swap="outerHTML"><p class="opacity-70">{{ verdict_pending }}</p></section>)";
constexpr std::string_view FINISHED_FRAGMENT =
    R"(<section class="mt-8" id="verdict"><p class="opacity-70">{{ verdict_label }}</p></section>)";

/// The stored verdict is a closed enum; an unrecognised value is reported as judged rather than echoed.
auto verdict_label(const Locale& locale, std::string_view verdict) -> std::string {
  if (verdict == "ok") {
    return label(locale, "Accepted", "ผ่าน");
  }
  if (verdict == "wrong_answer") {
    return label(locale, "Wrong answer", "คำตอบไม่ถูก");
  }
  if (verdict == "wrong_format") {
    return label(locale, "Wrong output format", "รูปแบบผลลัพธ์ไม่ถูก");
  }
  if (verdict == "partially_correct") {
    return label(locale, "Partially correct", "ถูกบางส่วน");
  }
  if (verdict == "failed") {
    return label(locale, "Failed", "ไม่ผ่าน");
  }
  if (verdict == "check_failed") {
    return label(locale, "Checker failed", "ตัวตรวจสอบล้มเหลว");
  }
  return label(locale, "Judged", "ตรวจแล้ว");
}

}  // namespace

auto verdict_fragment_model(const Locale& locale, std::string_view task_id,
                            const VerdictView& view) -> nlohmann::json {
  auto data = nlohmann::json{};
  data["verdict_url"] = "/tasks/" + std::string{task_id} + "/verdict";
  data["verdict_pending"] = verdict_pending_label(locale);
  data["verdict_label"] = verdict_label(locale, view.verdict);
  return data;
}

auto verdict_fragment_source(bool is_pending) -> std::string_view {
  return is_pending ? PENDING_FRAGMENT : FINISHED_FRAGMENT;
}

}  // namespace cms::api
