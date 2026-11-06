#include <cms/api/auth.hpp>

#include <array>
#include <cstddef>
#include <exception>
#include <string>
#include <string_view>

#include <sodium.h>
#include <sqlpp23/sqlpp23.h>

#include <cms/api/audit.hpp>
#include <cms/api/settings_defaults.hpp>
#include <cms/db/tables.hpp>

namespace cms::api {
namespace {

constexpr std::string_view ACTIVE_STATUS = "active";
constexpr std::string_view ACTION_PASSWORD = "account.password";
constexpr std::string_view INVOKED_BY = "api";
constexpr std::int64_t BYTES_PER_KIB = 1024;
constexpr std::string_view LEARNER_KIND = "learner";
constexpr std::string_view ADMIN_KIND = "admin";
constexpr std::string_view SYSTEM_KIND = "system";

auto random_generator_ready() -> bool {
  static const auto ready = sodium_init() >= 0;
  return ready;
}

/// The one refusal an unknown address and a wrong password share, so neither can be told from the other.
auto credentials_refused() -> Error {
  return Error::make(ErrorCode::permission_denied, "the email or password is wrong");
}

/// Iterations and memory come from the catalogue so a host can raise the cost; the defaults are declared in
/// settings_defaults.hpp, so an absent row still hashes at the declared parameters.
auto hash_password(std::string_view password, const SettingsSnapshot& settings)
    -> Result<std::string> {
  if (!random_generator_ready()) {
    return std::unexpected(Error::make(ErrorCode::unavailable, "libsodium did not initialise"));
  }
  const auto iterations = setting_int(settings, "identity.argon2_iterations");
  const auto memory_kib = setting_int(settings, "identity.argon2_memory_kib");
  if (iterations <= 0 || memory_kib <= 0) {
    return std::unexpected(
        Error::make(ErrorCode::invalid_argument,
                    "identity.argon2_iterations and identity.argon2_memory_kib must be positive"));
  }
  auto hash = std::array<char, crypto_pwhash_STRBYTES>{};
  const auto failed =
      crypto_pwhash_str(hash.data(), password.data(),
                        static_cast<unsigned long long>(password.size()),
                        static_cast<unsigned long long>(iterations),
                        static_cast<std::size_t>(memory_kib) * BYTES_PER_KIB) != 0;
  if (failed) {
    return std::unexpected(Error::make(ErrorCode::unavailable, "the password could not be hashed"));
  }
  return std::string{hash.data()};
}

auto stored_hash_check(const std::string& hash, std::string_view password) -> bool {
  return crypto_pwhash_str_verify(hash.c_str(), password.data(), password.size()) == 0;
}

struct AccountRow {
  std::string account_id;
  std::string status;
  std::string hash;
};

auto learner_row(db::connection& database, std::string_view email) -> AccountRow {
  auto row = AccountRow{};
  for (const auto& selected : database(sqlpp::select(db::learnerAccounts.id,
                                                      db::learnerAccounts.status,
                                                      db::learnerAccounts.passwordHash)
                                           .from(db::learnerAccounts)
                                           .where(db::learnerAccounts.email == std::string{email}))) {
    row.account_id = std::string{selected.id};
    row.status = std::string{selected.status};
    // Copied out of the row: the connector's buffers do not outlive the loop.
    row.hash = selected.passwordHash.has_value() ? std::string{*selected.passwordHash} : std::string{};
  }
  return row;
}

auto admin_row(db::connection& database, std::string_view handle) -> AccountRow {
  auto row = AccountRow{};
  for (const auto& selected : database(sqlpp::select(db::adminAccounts.id,
                                                     db::adminAccounts.status,
                                                     db::adminAccounts.passwordHash)
                                          .from(db::adminAccounts)
                                          .where(db::adminAccounts.handle == std::string{handle}))) {
    row.account_id = std::string{selected.id};
    row.status = std::string{selected.status};
    row.hash = selected.passwordHash.has_value() ? std::string{*selected.passwordHash} : std::string{};
  }
  return row;
}

auto row_is_on(db::connection& database, const auto& table, std::string_view account_id) -> bool {
  auto found = false;
  for (const auto& selected : database(sqlpp::select(table.id).from(table).where(
           table.id == std::string{account_id}))) {
    (void)selected;
    found = true;
  }
  return found;
}

/// The audit table's own CHECK refuses a row whose kind does not match its id column, so the table an id
/// lives in is read here rather than inferred from the uuid.
auto account_kind_of(db::connection& database, std::string_view account_id)
    -> std::string_view {
  if (row_is_on(database, db::learnerAccounts, account_id)) {
    return LEARNER_KIND;
  }
  if (row_is_on(database, db::adminAccounts, account_id)) {
    return ADMIN_KIND;
  }
  return SYSTEM_KIND;
}

}  // namespace

auto validate_password(const SettingsSnapshot& settings, std::string_view password)
    -> Result<void> {
  const auto minimum = setting_int(settings, "identity.password_min_length");
  if (minimum <= 0) {
    return std::unexpected(Error::make(ErrorCode::invalid_argument,
                                       "identity.password_min_length must be positive"));
  }
  if (static_cast<std::int64_t>(password.size()) < minimum) {
    return std::unexpected(
        Error::make(ErrorCode::invalid_argument, "the password must be at least " +
                                                     std::to_string(minimum) + " characters"));
  }
  return {};
}

auto set_password(db::connection& database, const SettingsSnapshot& settings,
                  std::string_view principal_id, std::string_view password) -> Result<void> {
  const auto valid = validate_password(settings, password);
  if (!valid) {
    return valid;
  }
  const auto hash = hash_password(password, settings);
  if (!hash) {
    return std::unexpected(hash.error());
  }
  try {
    const auto learner_changed =
        database(sqlpp::update(db::learnerAccounts)
                     .set(db::learnerAccounts.passwordHash = *hash)
                     .where(db::learnerAccounts.id == std::string{principal_id}));
    if (learner_changed == 0) {
      const auto admin_changed =
          database(sqlpp::update(db::adminAccounts)
                       .set(db::adminAccounts.passwordHash = *hash)
                       .where(db::adminAccounts.id == std::string{principal_id}));
      if (admin_changed == 0) {
        return std::unexpected(
            Error::make(ErrorCode::not_found, "no account called " + std::string{principal_id}));
      }
    }
    record_audit(database, account_kind_of(database, principal_id), principal_id, INVOKED_BY,
                 ACTION_PASSWORD, principal_id);
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }
  return {};
}

auto authenticate(db::connection& database, std::string_view identifier,
                  std::string_view password) -> Result<Account> {
  if (identifier.empty() || password.empty()) {
    return std::unexpected(credentials_refused());
  }
  if (!random_generator_ready()) {
    return std::unexpected(Error::make(ErrorCode::unavailable, "libsodium did not initialise"));
  }

  auto row = AccountRow{};
  auto kind = std::string_view{};
  try {
    row = learner_row(database, identifier);
    if (!row.account_id.empty()) {
      kind = LEARNER_KIND;
    } else {
      row = admin_row(database, identifier);
      kind = ADMIN_KIND;
    }
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }

  if (row.account_id.empty() || row.hash.empty()) {
    return std::unexpected(credentials_refused());
  }
  if (!stored_hash_check(row.hash, password)) {
    return std::unexpected(credentials_refused());
  }
  if (row.status != ACTIVE_STATUS) {
    return std::unexpected(credentials_refused());
  }
  return Account{.kind = std::string{kind}, .id = row.account_id};
}

auto end_session(db::connection& database, std::string_view session_id) -> Result<void> {
  if (session_id.empty()) {
    return {};
  }
  try {
    database(sqlpp::update(db::sessions)
                 .set(db::sessions.revokedAt = db::now_stamp())
                 .where(db::sessions.id == std::string{session_id} &&
                        db::sessions.revokedAt.is_null()));
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }
  return {};
}

}  // namespace cms::api
