#include <cms/api/auth_view.hpp>

#include <string>
#include <string_view>

namespace cms::api {
namespace {

struct SigninWords {
  std::string_view title;
  std::string_view lede;
  std::string_view email_label;
  std::string_view password_label;
  std::string_view submit_label;
  std::string_view signup_label;
};

constexpr SigninWords THAI_SIGNIN{.title = "เข้าสู่ระบบ",
                                  .lede = "ใช้บัญชีที่ยืนยันอีเมลแล้วเพื่อเข้าสู่ระบบ",
                                  .email_label = "อีเมล",
                                  .password_label = "รหัสผ่าน",
                                  .submit_label = "เข้าสู่ระบบ",
                                  .signup_label = "ยังไม่มีบัญชี? สมัครสมาชิก"};

constexpr SigninWords ENGLISH_SIGNIN{.title = "Sign in",
                                     .lede = "Use an account whose email is verified.",
                                     .email_label = "Email",
                                     .password_label = "Password",
                                     .submit_label = "Sign in",
                                     .signup_label = "No account? Sign up"};

struct SignupWords {
  std::string_view title;
  std::string_view lede;
  std::string_view email_label;
  std::string_view handle_label;
  std::string_view password_label;
  std::string_view submit_label;
  std::string_view signin_label;
};

constexpr SignupWords THAI_SIGNUP{.title = "สมัครสมาชิก",
                                  .lede = "สร้างบัญชี แล้วยืนยันอีเมลของคุณก่อนเข้าสู่ระบบ",
                                  .email_label = "อีเมล",
                                  .handle_label = "ชื่อผู้ใช้",
                                  .password_label = "รหัสผ่าน",
                                  .submit_label = "สมัครสมาชิก",
                                  .signin_label = "มีบัญชีอยู่แล้ว? เข้าสู่ระบบ"};

constexpr SignupWords ENGLISH_SIGNUP{.title = "Sign up",
                                     .lede = "Create an account, then verify your email before signing in.",
                                     .email_label = "Email",
                                     .handle_label = "Handle",
                                     .password_label = "Password",
                                     .submit_label = "Sign up",
                                     .signin_label = "Already have an account? Sign in"};

}  // namespace

auto signin_notice(const Locale& locale, SigninNotice notice) -> std::string {
  switch (notice) {
    case SigninNotice::registered:
      return label(locale,
                   "Your account was created. Open the link we sent to verify your address.",
                   "สร้างบัญชีแล้ว เปิดลิงก์ที่เราส่งไปเพื่อยืนยันอีเมล");
    case SigninNotice::verified:
      return label(locale, "Your address is verified. Sign in.",
                   "ยืนยันอีเมลแล้ว เข้าสู่ระบบได้");
    case SigninNotice::approval_pending:
      return label(locale,
                   "Your address is verified. An operator must approve the account before you can sign in.",
                   "ยืนยันอีเมลแล้ว ผู้ดูแลต้องอนุมัติบัญชีก่อนจึงจะเข้าสู่ระบบได้");
    case SigninNotice::none:
      return {};
  }
  return {};
}

auto signin_model(const Locale& locale, std::string_view site_name, std::string_view message)
    -> nlohmann::json {
  auto data = shell_model(locale, site_name);
  data["title"] = label(locale, ENGLISH_SIGNIN.title, THAI_SIGNIN.title);
  data["lede"] = label(locale, ENGLISH_SIGNIN.lede, THAI_SIGNIN.lede);
  data["email_label"] = label(locale, ENGLISH_SIGNIN.email_label, THAI_SIGNIN.email_label);
  data["password_label"] =
      label(locale, ENGLISH_SIGNIN.password_label, THAI_SIGNIN.password_label);
  data["submit_label"] = label(locale, ENGLISH_SIGNIN.submit_label, THAI_SIGNIN.submit_label);
  data["signup_label"] = label(locale, ENGLISH_SIGNIN.signup_label, THAI_SIGNIN.signup_label);
  data["signup_href"] = "/signup";
  data["action"] = "/signin";
  data["message"] = std::string{message};
  return data;
}

auto signup_model(const Locale& locale, std::string_view site_name, std::string_view message)
    -> nlohmann::json {
  auto data = shell_model(locale, site_name);
  data["title"] = label(locale, ENGLISH_SIGNUP.title, THAI_SIGNUP.title);
  data["lede"] = label(locale, ENGLISH_SIGNUP.lede, THAI_SIGNUP.lede);
  data["email_label"] = label(locale, ENGLISH_SIGNUP.email_label, THAI_SIGNUP.email_label);
  data["handle_label"] = label(locale, ENGLISH_SIGNUP.handle_label, THAI_SIGNUP.handle_label);
  data["password_label"] =
      label(locale, ENGLISH_SIGNUP.password_label, THAI_SIGNUP.password_label);
  data["submit_label"] = label(locale, ENGLISH_SIGNUP.submit_label, THAI_SIGNUP.submit_label);
  data["signin_label"] = label(locale, ENGLISH_SIGNUP.signin_label, THAI_SIGNUP.signin_label);
  data["signin_href"] = "/signin";
  data["action"] = "/signup";
  data["message"] = std::string{message};
  return data;
}

}  // namespace cms::api
