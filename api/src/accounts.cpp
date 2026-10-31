#include <cms/api/accounts.hpp>

#include <array>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <sodium.h>
#include <sqlpp23/sqlpp23.h>

#include <cms/api/mailer.hpp>
#include <cms/api/settings_defaults.hpp>
#include <cms/api/uuid.hpp>
#include <cms/db/tables.hpp>

namespace cms::api {
namespace {

constexpr std::int64_t VERIFICATION_LIFETIME_SECONDS = 24 * 60 * 60;
constexpr std::size_t TOKEN_BYTES = 32;
constexpr std::size_t HEX_DIGITS_PER_BYTE = 2;
constexpr unsigned NIBBLE_SHIFT = 4;
constexpr unsigned NIBBLE_MASK = 0x0fu;

constexpr std::string_view PURPOSE_VERIFY_EMAIL = "verify_email";
constexpr std::string_view ACTION_SIGNUP = "account.signup";
constexpr std::string_view ACTION_VERIFY = "account.verify";
constexpr std::string_view ACTION_APPROVE = "account.approve";
constexpr std::string_view ACTION_SESSION = "session.create";
constexpr std::string_view INVOKED_BY = "api";

auto to_hex(std::span<const unsigned char> bytes) -> std::string {
  constexpr std::string_view DIGITS = "0123456789abcdef";
  auto text = std::string{};
  text.reserve(bytes.size() * HEX_DIGITS_PER_BYTE);
  for (const auto byte : bytes) {
    const auto value = static_cast<unsigned>(byte);
    text.push_back(DIGITS[value >> NIBBLE_SHIFT]);
    text.push_back(DIGITS[value & NIBBLE_MASK]);
  }
  return text;
}

auto hash_token(std::string_view token) -> std::string {
  auto digest = std::array<unsigned char, crypto_hash_sha256_BYTES>{};
  crypto_hash_sha256(digest.data(), reinterpret_cast<const unsigned char*>(token.data()),
                     static_cast<unsigned long long>(token.size()));
  return to_hex(digest);
}

auto random_token() -> std::string {
  auto bytes = std::array<unsigned char, TOKEN_BYTES>{};
  randombytes_buf(bytes.data(), bytes.size());
  return to_hex(bytes);
}

auto now_stamp() -> cms::db::timestamp {
  return std::chrono::time_point_cast<cms::db::timestamp::duration>(
      std::chrono::system_clock::now());
}

auto stamp_after(std::int64_t seconds) -> cms::db::timestamp {
  return now_stamp() + std::chrono::seconds{seconds};
}

auto domain_of(std::string_view email) -> std::string {
  const auto at = email.rfind('@');
  if (at == std::string_view::npos || at + 1 >= email.size()) {
    return {};
  }
  auto domain = std::string{email.substr(at + 1)};
  for (auto& character : domain) {
    character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  }
  return domain;
}

/// An empty allowlist means "no domain restriction"; the gate is `site.registration_open`. A domain
/// matches when it is the listed domain or a subdomain of it.
auto domain_allowed(std::string_view domain, std::string_view allowlist) -> bool {
  auto remaining = allowlist;
  auto any = false;
  while (!remaining.empty()) {
    const auto separator = remaining.find_first_of(", \t");
    const auto entry = remaining.substr(0, separator);
    if (!entry.empty()) {
      any = true;
      auto lowered = std::string{entry};
      for (auto& character : lowered) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
      }
      const auto suffix = "." + lowered;
      if (domain == lowered ||
          (domain.size() > suffix.size() &&
           domain.substr(domain.size() - suffix.size()) == suffix)) {
        return true;
      }
    }
    if (separator == std::string_view::npos) {
      break;
    }
    remaining.remove_prefix(separator + 1);
  }
  return !any;
}

auto record_audit(db::connection& database, std::string_view actor_id, std::string_view action,
                  std::string_view subject) -> void {
  database(sqlpp::insert_into(db::auditEvents).set(
      db::auditEvents.actorId = std::string{actor_id},
      db::auditEvents.invokedBy = std::string{INVOKED_BY},
      db::auditEvents.action = std::string{action}, db::auditEvents.subject = std::string{subject}));
}

auto failure(std::string_view text) -> Error {
  return Error::make(ErrorCode::unavailable, text);
}

}  // namespace

auto issue_verification(db::connection& database, std::string_view principal_id)
    -> Result<std::string> {
  auto token = random_token();
  try {
    database(sqlpp::insert_into(db::principalTokens).set(
        db::principalTokens.principalId = std::string{principal_id},
        db::principalTokens.purpose = std::string{PURPOSE_VERIFY_EMAIL},
        db::principalTokens.tokenHash = hash_token(token),
        db::principalTokens.expiresAt = stamp_after(VERIFICATION_LIFETIME_SECONDS)));
  } catch (const std::exception& error) {
    return std::unexpected(failure(error.what()));
  }
  return token;
}

auto sign_up(db::connection& database, const SettingsSnapshot& settings, std::string_view email,
             std::string_view handle) -> Result<SignupOutcome> {
  if (!setting_bool(settings, "site.registration_open")) {
    return std::unexpected(Error::make(ErrorCode::permission_denied, "registration is closed"));
  }

  const auto domain = domain_of(email);
  if (domain.empty()) {
    return std::unexpected(
        Error::make(ErrorCode::invalid_argument, "the email address has no domain"));
  }
  if (!domain_allowed(domain, setting_text(settings, "site.registration_domain_allowlist"))) {
    return std::unexpected(Error::make(ErrorCode::permission_denied,
                                       "the email domain " + domain + " is not allowed to register"));
  }

  const auto principal_id = uuid_v4();
  try {
    database(sqlpp::insert_into(db::principals).set(
        db::principals.id = principal_id, db::principals.kind = std::string{"user"},
        db::principals.handle = std::string{handle}, db::principals.email = std::string{email},
        db::principals.status = std::string{"pending"}));
    record_audit(database, principal_id, ACTION_SIGNUP, principal_id);
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::conflict, error.what()));
  }

  const auto token = issue_verification(database, principal_id);
  if (!token) {
    return std::unexpected(token.error());
  }

  const auto sent = send_mail(settings, {.to = std::string{email},
                                         .subject = "Your CMS Learn verification code",
                                         .body = "Enter this code to verify your address: " + *token});
  if (!sent) {
    return std::unexpected(sent.error());
  }
  return SignupOutcome{.principal_id = principal_id, .mail_sent = *sent};
}

auto verify_email(db::connection& database, const SettingsSnapshot& settings, std::string_view token)
    -> Result<bool> {
  const auto hash = hash_token(token);

  auto principal_id = std::string{};
  for (const auto& row : database(sqlpp::select(db::principalTokens.principalId)
                                      .from(db::principalTokens)
                                      .where(db::principalTokens.tokenHash == hash &&
                                             db::principalTokens.purpose ==
                                                 std::string{PURPOSE_VERIFY_EMAIL} &&
                                             db::principalTokens.usedAt.is_null() &&
                                             db::principalTokens.expiresAt > now_stamp()))) {
    principal_id = std::string{row.principalId};
  }
  if (principal_id.empty()) {
    return std::unexpected(
        Error::make(ErrorCode::not_found, "that verification code is not valid or has expired"));
  }

  const auto requires_approval = setting_bool(settings, "site.registration_requires_approval");
  const auto status = requires_approval ? "pending" : "active";
  try {
    database(sqlpp::update(db::principalTokens)
                 .set(db::principalTokens.usedAt = now_stamp())
                 .where(db::principalTokens.tokenHash == hash));
    database(sqlpp::update(db::principals)
                 .set(db::principals.emailVerifiedAt = now_stamp(),
                      db::principals.status = std::string{status})
                 .where(db::principals.id == principal_id));
    record_audit(database, principal_id, ACTION_VERIFY, principal_id);
  } catch (const std::exception& error) {
    return std::unexpected(failure(error.what()));
  }
  return !requires_approval;
}

auto approve_account(db::connection& database, std::string_view principal_id,
                     std::string_view actor_id) -> Result<void> {
  try {
    const auto changed = database(sqlpp::update(db::principals)
                                      .set(db::principals.status = std::string{"active"})
                                      .where(db::principals.id == std::string{principal_id} &&
                                             !db::principals.emailVerifiedAt.is_null()));
    if (changed == 0) {
      return std::unexpected(Error::make(
          ErrorCode::invalid_argument,
          "only an account that has verified its address can be approved"));
    }
    record_audit(database, actor_id, ACTION_APPROVE, principal_id);
  } catch (const std::exception& error) {
    return std::unexpected(failure(error.what()));
  }
  return {};
}

auto create_session(db::connection& database, std::string_view principal_id,
                    std::int64_t lifetime_seconds) -> Result<std::string> {
  auto session_id = uuid_v4();
  try {
    auto active = database(sqlpp::select(db::principals.id)
                                     .from(db::principals)
                                     .where(db::principals.id == std::string{principal_id} &&
                                            db::principals.status == std::string{"active"}));
    auto found = false;
    for (const auto& row : active) {
      (void)row;
      found = true;
    }
    if (!found) {
      return std::unexpected(Error::make(ErrorCode::permission_denied,
                                         "only an active account can open a session"));
    }

    database(sqlpp::insert_into(db::sessions).set(
        db::sessions.id = session_id, db::sessions.principalId = std::string{principal_id},
        db::sessions.expiresAt = stamp_after(lifetime_seconds)));
    record_audit(database, principal_id, ACTION_SESSION, session_id);
  } catch (const std::exception& error) {
    return std::unexpected(failure(error.what()));
  }
  return session_id;
}

}  // namespace cms::api
