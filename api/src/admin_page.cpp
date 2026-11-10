#include <cms/api/admin_routes.hpp>

#include <array>
#include <exception>
#include <string>
#include <string_view>
#include <vector>

#include <sqlpp23/sqlpp23.h>

#include <cms/db/tables.hpp>
#include <cms/error.hpp>

namespace cms::api {
namespace {

struct PanelWords {
  std::string_view title;
  std::string_view lede;
  std::string_view roster;
  std::string_view empty;
  std::string_view create_learner;
  std::string_view create_admin;
  std::string_view approve;
  std::string_view revoke_admin;
  std::string_view issue_verification;
  std::string_view submit;
  std::string_view id;
  std::string_view handle;
  std::string_view status;
  std::string_view level;
  std::string_view email;
  std::string_view password;
  std::string_view role;
  std::string_view learner_id;
  std::string_view admin_id;
};

constexpr PanelWords THAI_PANEL{.title = "แผงผู้ดูแลระบบ",
                                .lede = "ลงชื่อเข้าใช้ในชื่อ",
                                .roster = "ผู้เรียน",
                                .empty = "ยังไม่มีบัญชีผู้เรียน",
                                .create_learner = "สร้างบัญชีผู้เรียน",
                                .create_admin = "สร้างบัญชีผู้ดูแลระบบ",
                                .approve = "อนุมัติผู้เรียน",
                                .revoke_admin = "ถอดสิทธิ์ผู้ดูแลระบบ",
                                .issue_verification = "ออกโทเคนยืนยันอีเมล",
                                .submit = "บันทึก",
                                .id = "รหัส",
                                .handle = "ชื่อผู้ใช้",
                                .status = "สถานะ",
                                .level = "ระดับ",
                                .email = "อีเมล",
                                .password = "รหัสผ่าน",
                                .role = "รหัสบทบาท คั่นด้วยจุลภาค",
                                .learner_id = "รหัสผู้เรียน",
                                .admin_id = "รหัสผู้ดูแลระบบ"};

constexpr PanelWords ENGLISH_PANEL{.title = "Administrator panel",
                                   .lede = "Signed in as",
                                   .roster = "Learners",
                                   .empty = "No learner accounts yet",
                                   .create_learner = "Create a learner",
                                   .create_admin = "Create an administrator",
                                   .approve = "Approve a learner",
                                   .revoke_admin = "Revoke an administrator",
                                   .issue_verification = "Issue a verification token",
                                   .submit = "Save",
                                   .id = "Id",
                                   .handle = "Handle",
                                   .status = "Status",
                                   .level = "Level",
                                   .email = "Email",
                                   .password = "Password",
                                   .role = "Role ids, comma separated",
                                   .learner_id = "Learner id",
                                   .admin_id = "Administrator id"};

/// The canned sentences a completed action shows. Like the sign-in page's notices, no request-supplied
/// text ever becomes page copy.
struct PanelNotice {
  std::string_view code;
  std::string_view english;
  std::string_view thai;
};

constexpr std::array<PanelNotice, 5> NOTICES{{
    {"learner-created", "Created the learner account.", "สร้างบัญชีผู้เรียนแล้ว"},
    {"admin-created", "Created the administrator account.", "สร้างบัญชีผู้ดูแลระบบแล้ว"},
    {"learner-approved", "Approved the learner account.", "อนุมัติบัญชีผู้เรียนแล้ว"},
    {"admin-revoked", "Revoked the administrator account.", "ถอดสิทธิ์บัญชีผู้ดูแลระบบแล้ว"},
    {"verification-issued", "Issued a verification token.", "ออกโทเคนยืนยันแล้ว"},
}};

auto pick(const Locale& locale, std::string_view english, std::string_view thai) -> std::string {
  return label(locale, english, thai);
}

auto notice_sentence(const Locale& locale, std::string_view code) -> std::string {
  for (const auto& notice : NOTICES) {
    if (notice.code == code) {
      return label(locale, notice.english, notice.thai);
    }
  }
  return {};
}

auto fill_words(const Locale& locale, nlohmann::json& data) -> void {
  data["title"] = pick(locale, ENGLISH_PANEL.title, THAI_PANEL.title);
  data["lede"] = pick(locale, ENGLISH_PANEL.lede, THAI_PANEL.lede);
  data["roster_label"] = pick(locale, ENGLISH_PANEL.roster, THAI_PANEL.roster);
  data["empty_label"] = pick(locale, ENGLISH_PANEL.empty, THAI_PANEL.empty);
  data["create_learner_label"] = pick(locale, ENGLISH_PANEL.create_learner, THAI_PANEL.create_learner);
  data["create_admin_label"] = pick(locale, ENGLISH_PANEL.create_admin, THAI_PANEL.create_admin);
  data["approve_label"] = pick(locale, ENGLISH_PANEL.approve, THAI_PANEL.approve);
  data["revoke_admin_label"] = pick(locale, ENGLISH_PANEL.revoke_admin, THAI_PANEL.revoke_admin);
  data["issue_verification_label"] =
      pick(locale, ENGLISH_PANEL.issue_verification, THAI_PANEL.issue_verification);
  data["submit_label"] = pick(locale, ENGLISH_PANEL.submit, THAI_PANEL.submit);
  data["id_label"] = pick(locale, ENGLISH_PANEL.id, THAI_PANEL.id);
  data["handle_label"] = pick(locale, ENGLISH_PANEL.handle, THAI_PANEL.handle);
  data["status_label"] = pick(locale, ENGLISH_PANEL.status, THAI_PANEL.status);
  data["level_label"] = pick(locale, ENGLISH_PANEL.level, THAI_PANEL.level);
  data["email_label"] = pick(locale, ENGLISH_PANEL.email, THAI_PANEL.email);
  data["password_label"] = pick(locale, ENGLISH_PANEL.password, THAI_PANEL.password);
  data["role_label"] = pick(locale, ENGLISH_PANEL.role, THAI_PANEL.role);
  data["learner_id_label"] = pick(locale, ENGLISH_PANEL.learner_id, THAI_PANEL.learner_id);
  data["admin_id_label"] = pick(locale, ENGLISH_PANEL.admin_id, THAI_PANEL.admin_id);
}

}  // namespace

auto read_learner_roster(db::connection& database) -> Result<std::vector<LearnerRow>> {
  auto rows = std::vector<LearnerRow>{};
  try {
    for (const auto& row :
         database(sqlpp::select(db::learnerAccounts.id, db::learnerAccounts.handle,
                                db::learnerAccounts.status, db::learnerAccounts.levelId)
                      .from(db::learnerAccounts)
                      .order_by(db::learnerAccounts.handle.asc()))) {
      rows.push_back(LearnerRow{.id = std::string{row.id},
                                .handle = std::string{row.handle},
                                .status = std::string{row.status},
                                .level = row.levelId.has_value() ? std::string{*row.levelId}
                                                                 : std::string{}});
    }
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }
  return rows;
}

auto read_admin_handle(db::connection& database, std::string_view admin_id) -> Result<std::string> {
  try {
    for (const auto& row : database(sqlpp::select(db::adminAccounts.handle)
                                        .from(db::adminAccounts)
                                        .where(db::adminAccounts.id == std::string{admin_id}))) {
      return std::string{row.handle};
    }
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }
  return std::unexpected(Error::make(ErrorCode::not_found,
                                     "the signed-in administrator no longer exists"));
}

auto panel_model(const Locale& locale, std::string_view site_name, const PanelHome& home)
    -> nlohmann::json {
  auto data = shell_model(locale, site_name, LevelView{});
  fill_words(locale, data);
  data["admin_handle"] = home.admin_handle;
  data["csrf_token"] = home.csrf_token;
  const auto notice = notice_sentence(locale, home.notice);
  data["notice"] = notice;
  data["has_notice"] = !notice.empty();
  data["message"] = home.message;
  data["has_message"] = !home.message.empty();
  data["learners"] = nlohmann::json::array();
  for (const auto& learner : home.learners) {
    data["learners"].push_back({{"id", learner.id},
                                {"handle", learner.handle},
                                {"status", learner.status},
                                {"level", learner.level}});
  }
  data["has_learners"] = !home.learners.empty();
  return data;
}

}  // namespace cms::api
