#include <cms/api/accounts.hpp>

#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <sodium.h>
#include <sqlpp23/sqlpp23.h>

#include <cms/api/audit.hpp>
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

namespace count_alias {
SQLPP_CREATE_NAME_TAG(row_count);
}  // namespace count_alias

constexpr std::string_view ACCOUNT_KIND_LEARNER = "learner";
constexpr std::string_view ACCOUNT_KIND_ADMIN = "admin";
constexpr std::string_view ACCOUNT_KIND_SYSTEM = "system";
constexpr std::string_view ADMIN_STATUS_ACTIVE = "active";
constexpr std::string_view BOOTSTRAP_SYSTEM_HANDLE = "system:bootstrap";
constexpr std::string_view ACTION_BOOTSTRAP = "bootstrap.completed";

constexpr std::string_view STATUS_PENDING = "pending";
constexpr std::string_view STATUS_ACTIVE = "active";

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

auto failure(std::string_view text) -> Error {
  return Error::make(ErrorCode::unavailable, text);
}

auto counts_at_least_one(db::connection& database, const auto& statement) -> bool {
  auto found = false;
  for (const auto& row : database(statement)) {
    found = row.row_count > 0;
  }
  return found;
}

auto learner_is_active(db::connection& database, std::string_view learner_id) -> bool {
  const auto one = sqlpp::value(1);
  return counts_at_least_one(
      database, sqlpp::select(sqlpp::count(one).as(count_alias::row_count))
                    .from(db::learnerAccounts)
                    .where(db::learnerAccounts.id == std::string{learner_id} &&
                           db::learnerAccounts.status == std::string{STATUS_ACTIVE}));
}

auto admin_is_active(db::connection& database, std::string_view admin_id) -> bool {
  const auto one = sqlpp::value(1);
  return counts_at_least_one(
      database, sqlpp::select(sqlpp::count(one).as(count_alias::row_count))
                    .from(db::adminAccounts)
                    .where(db::adminAccounts.id == std::string{admin_id} &&
                           db::adminAccounts.status == std::string{ADMIN_STATUS_ACTIVE}));
}

auto system_exists(db::connection& database, std::string_view system_id) -> bool {
  const auto one = sqlpp::value(1);
  return counts_at_least_one(database, sqlpp::select(sqlpp::count(one).as(count_alias::row_count))
                                           .from(db::systemAccounts)
                                           .where(db::systemAccounts.id == std::string{system_id}));
}

}  // namespace

auto issue_verification(db::connection& database, std::string_view principal_id)
    -> Result<std::string> {
  auto token = random_token();
  try {
    database(sqlpp::insert_into(db::learnerTokens).set(
        db::learnerTokens.learnerId = std::string{principal_id},
        db::learnerTokens.purpose = std::string{PURPOSE_VERIFY_EMAIL},
        db::learnerTokens.tokenHash = hash_token(token),
        db::learnerTokens.expiresAt = db::stamp_after(VERIFICATION_LIFETIME_SECONDS)));
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

  const auto requires_email = setting_bool(settings, "site.registration_requires_email");
  if (requires_email) {
    const auto domain = domain_of(email);
    if (domain.empty()) {
      return std::unexpected(
          Error::make(ErrorCode::invalid_argument, "the email address has no domain"));
    }
    if (!domain_allowed(domain, setting_text(settings, "site.registration_domain_allowlist"))) {
      return std::unexpected(Error::make(ErrorCode::permission_denied,
                                         "the email domain " + domain + " is not allowed to register"));
    }
  }

  const auto principal_id = uuid_v4();
  try {
    if (email.empty()) {
      database(sqlpp::insert_into(db::learnerAccounts).set(
          db::learnerAccounts.id = principal_id, db::learnerAccounts.handle = std::string{handle},
          db::learnerAccounts.status = std::string{STATUS_PENDING}));
    } else {
      database(sqlpp::insert_into(db::learnerAccounts).set(
          db::learnerAccounts.id = principal_id, db::learnerAccounts.handle = std::string{handle},
          db::learnerAccounts.email = std::string{email},
          db::learnerAccounts.status = std::string{STATUS_PENDING}));
    }
    record_audit(database, ACCOUNT_KIND_LEARNER, principal_id, INVOKED_BY, ACTION_SIGNUP,
                 principal_id);
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::conflict, error.what()));
  }

  // Nothing to verify when no address was given, so no token is issued and no mail is attempted.
  if (email.empty()) {
    return SignupOutcome{.principal_id = principal_id, .mail_sent = false};
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
  for (const auto& row :
       database(sqlpp::select(db::learnerTokens.learnerId)
                    .from(db::learnerTokens)
                    .where(db::learnerTokens.tokenHash == hash &&
                           db::learnerTokens.purpose == std::string{PURPOSE_VERIFY_EMAIL} &&
                           db::learnerTokens.usedAt.is_null() &&
                           db::learnerTokens.expiresAt > db::now_stamp()))) {
    principal_id = std::string{row.learnerId};
  }
  if (principal_id.empty()) {
    return std::unexpected(
        Error::make(ErrorCode::not_found, "that verification code is not valid or has expired"));
  }

  const auto requires_approval = setting_bool(settings, "site.registration_requires_approval");
  const auto status = requires_approval ? STATUS_PENDING : STATUS_ACTIVE;
  try {
    database(sqlpp::update(db::learnerTokens)
                 .set(db::learnerTokens.usedAt = db::now_stamp())
                 .where(db::learnerTokens.tokenHash == hash));
    database(sqlpp::update(db::learnerAccounts)
                 .set(db::learnerAccounts.emailVerifiedAt = db::now_stamp(),
                      db::learnerAccounts.status = std::string{status})
                 .where(db::learnerAccounts.id == principal_id));
    record_audit(database, ACCOUNT_KIND_LEARNER, principal_id, INVOKED_BY, ACTION_VERIFY,
                 principal_id);
  } catch (const std::exception& error) {
    return std::unexpected(failure(error.what()));
  }
  return !requires_approval;
}

auto approve_account(db::connection& database, std::string_view learner_id, const Actor& actor,
                     std::string_view invoked_by) -> Result<void> {
  try {
    const auto changed =
        database(sqlpp::update(db::learnerAccounts)
                     .set(db::learnerAccounts.status = std::string{STATUS_ACTIVE})
                     .where(db::learnerAccounts.id == std::string{learner_id} &&
                            !db::learnerAccounts.emailVerifiedAt.is_null()));
    if (changed == 0) {
      return std::unexpected(Error::make(
          ErrorCode::invalid_argument,
          "only an account that has verified its address can be approved"));
    }
    record_audit(database, actor.kind, actor.id, invoked_by, ACTION_APPROVE, learner_id);
  } catch (const std::exception& error) {
    return std::unexpected(failure(error.what()));
  }
  return {};
}

auto create_session(db::connection& database, std::string_view account_kind,
                    std::string_view account_id, std::int64_t lifetime_seconds)
    -> Result<std::string> {
  const auto is_learner = account_kind == ACCOUNT_KIND_LEARNER;
  const auto is_admin = account_kind == ACCOUNT_KIND_ADMIN;
  const auto is_system = account_kind == ACCOUNT_KIND_SYSTEM;
  if (!is_learner && !is_admin && !is_system) {
    return std::unexpected(Error::make(ErrorCode::invalid_argument,
                                       "unknown account kind: " + std::string{account_kind}));
  }
  auto session_id = uuid_v4();
  try {
    const auto is_active = is_learner ? learner_is_active(database, account_id)
                                      : (is_admin ? admin_is_active(database, account_id)
                                                  : system_exists(database, account_id));
    if (!is_active) {
      return std::unexpected(
          Error::make(ErrorCode::permission_denied, "only an active account can open a session"));
    }

    database(sqlpp::insert_into(db::sessions).set(
        db::sessions.id = session_id, db::sessions.accountKind = std::string{account_kind},
        db::sessions.learnerId =
            is_learner ? std::optional<std::string>{std::string{account_id}} : std::nullopt,
        db::sessions.adminId =
            is_admin ? std::optional<std::string>{std::string{account_id}} : std::nullopt,
        db::sessions.systemId =
            is_system ? std::optional<std::string>{std::string{account_id}} : std::nullopt,
        db::sessions.expiresAt = db::stamp_after(lifetime_seconds)));
    record_audit(database, account_kind, account_id, INVOKED_BY, ACTION_SESSION, session_id);
  } catch (const std::exception& error) {
    return std::unexpected(failure(error.what()));
  }
  return session_id;
}

auto bootstrap(db::connection& database, std::string_view admin_email, std::string_view admin_handle)
    -> Result<std::string> {
  (void)admin_email;
  auto existing = std::int64_t{0};
  try {
    const auto one = sqlpp::value(1);
    for (const auto& row : database(sqlpp::select(sqlpp::count(one).as(count_alias::row_count))
                                        .from(db::adminAccounts))) {
      existing = static_cast<std::int64_t>(row.row_count);
    }
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }
  if (existing != 0) {
    return std::unexpected(Error::make(
        ErrorCode::conflict, "this database already holds administrators; bootstrap runs once"));
  }

  const auto system_id = uuid_v4();
  const auto admin_id = uuid_v4();
  try {
    database(sqlpp::insert_into(db::systemAccounts).set(
        db::systemAccounts.id = system_id,
        db::systemAccounts.handle = std::string{BOOTSTRAP_SYSTEM_HANDLE}));
    database(sqlpp::insert_into(db::adminAccounts).set(
        db::adminAccounts.id = admin_id, db::adminAccounts.handle = std::string{admin_handle},
        db::adminAccounts.status = std::string{ADMIN_STATUS_ACTIVE}));
    record_audit(database, ACCOUNT_KIND_SYSTEM, system_id, INVOKED_BY, ACTION_BOOTSTRAP, admin_id);
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::conflict, error.what()));
  }
  return admin_id;
}

}  // namespace cms::api
