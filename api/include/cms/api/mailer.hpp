#pragma once

#include <string>

#include <cms/error.hpp>
#include <cms/settings_snapshot.hpp>

namespace cms::api {

/// @brief One message the service wants to deliver.
struct MailMessage {
  std::string to;
  std::string subject;
  std::string body;
};

/// @brief Sends @p message when `mail.enabled` is true, and does nothing when it is false.
///
/// @param settings The snapshot the mail settings are read from, so a change takes effect on the next
/// send without a restart.
/// @return Whether a message was sent. A disabled mailer is a successful no-op, not an error: the
/// operator turned it off, which is the default, and signup must still work.
[[nodiscard]] Result<bool> send_mail(const SettingsSnapshot& settings, const MailMessage& message);

}  // namespace cms::api
