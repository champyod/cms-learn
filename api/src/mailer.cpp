#include <cms/api/mailer.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <curl/curl.h>

#include <cms/api/settings_defaults.hpp>

namespace cms::api {
namespace {

constexpr long CONNECT_TIMEOUT_SECONDS = 10;
constexpr long SEND_TIMEOUT_SECONDS = 30;

struct curl_deleter {
  void operator()(CURL* handle) const { curl_easy_cleanup(handle); }
};

struct list_deleter {
  void operator()(curl_slist* list) const { curl_slist_free_all(list); }
};

auto ensure_curl_ready() -> bool {
  static const auto ready = [] { return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK; }();
  return ready;
}

auto read_from_string(char* buffer, std::size_t size, std::size_t count, void* userdata) noexcept
    -> std::size_t {
  auto* payload = static_cast<std::pair<const std::string*, std::size_t>*>(userdata);
  const auto remaining = payload->first->size() - payload->second;
  const auto wanted = size * count;
  const auto taken = remaining < wanted ? remaining : wanted;
  payload->first->copy(buffer, taken, payload->second);
  payload->second += taken;
  return taken;
}

auto message_text(const MailMessage& message, std::string_view from) -> std::string {
  auto text = std::string{};
  text += "From: " + std::string{from} + "\r\n";
  text += "To: " + message.to + "\r\n";
  text += "Subject: " + message.subject + "\r\n";
  text += "MIME-Version: 1.0\r\n";
  text += "Content-Type: text/plain; charset=utf-8\r\n";
  text += "\r\n";
  text += message.body;
  text += "\r\n";
  return text;
}

}  // namespace

auto send_mail(const SettingsSnapshot& settings, const MailMessage& message) -> Result<bool> {
  if (!setting_bool(settings, "mail.enabled")) {
    return false;
  }

  const auto host = setting_text(settings, "mail.host");
  const auto from = setting_text(settings, "mail.from_address");
  if (host.empty() || from.empty() || message.to.empty()) {
    return std::unexpected(Error::make(ErrorCode::invalid_argument,
                                       "mail is enabled but mail.host, mail.from_address or the "
                                       "recipient is empty"));
  }
  if (!ensure_curl_ready()) {
    return std::unexpected(Error::make(ErrorCode::unavailable, "libcurl could not initialise"));
  }

  auto handle = std::unique_ptr<CURL, curl_deleter>{curl_easy_init()};
  if (!handle) {
    return std::unexpected(Error::make(ErrorCode::unavailable, "libcurl returned no handle"));
  }

  const auto url = "smtp://" + host + ":" + std::to_string(setting_int(settings, "mail.port"));
  const auto body = message_text(message, from);
  auto offset = std::size_t{0};
  auto payload = std::pair<const std::string*, std::size_t>{&body, offset};

  auto recipients = std::unique_ptr<curl_slist, list_deleter>{curl_slist_append(nullptr, message.to.c_str())};
  if (!recipients) {
    return std::unexpected(Error::make(ErrorCode::unavailable, "libcurl could not hold a recipient"));
  }

  const auto username = setting_text(settings, "mail.username");
  const auto password = setting_text(settings, "mail.password");

  curl_easy_setopt(handle.get(), CURLOPT_URL, url.c_str());
  curl_easy_setopt(handle.get(), CURLOPT_MAIL_FROM, from.c_str());
  curl_easy_setopt(handle.get(), CURLOPT_MAIL_RCPT, recipients.get());
  curl_easy_setopt(handle.get(), CURLOPT_UPLOAD, 1L);
  curl_easy_setopt(handle.get(), CURLOPT_READFUNCTION, read_from_string);
  curl_easy_setopt(handle.get(), CURLOPT_READDATA, &payload);
  curl_easy_setopt(handle.get(), CURLOPT_CONNECTTIMEOUT, CONNECT_TIMEOUT_SECONDS);
  curl_easy_setopt(handle.get(), CURLOPT_TIMEOUT, SEND_TIMEOUT_SECONDS);
  curl_easy_setopt(handle.get(), CURLOPT_USE_SSL,
                   setting_bool(settings, "mail.starttls") ? CURLUSESSL_ALL : CURLUSESSL_NONE);
  if (!username.empty()) {
    curl_easy_setopt(handle.get(), CURLOPT_USERNAME, username.c_str());
    curl_easy_setopt(handle.get(), CURLOPT_PASSWORD, password.c_str());
  }

  const auto status = curl_easy_perform(handle.get());
  if (status != CURLE_OK) {
    return std::unexpected(Error::make(ErrorCode::unavailable,
                                       std::string{"the mail server refused the message: "} +
                                           curl_easy_strerror(status)));
  }
  return true;
}

}  // namespace cms::api
