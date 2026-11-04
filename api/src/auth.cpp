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
    const auto changed =
        database(sqlpp::update(db::principals)
                     .set(db::principals.passwordHash = *hash)
                     .where(db::principals.id == std::string{principal_id}));
    if (changed == 0) {
      return std::unexpected(
          Error::make(ErrorCode::not_found, "no account called " + std::string{principal_id}));
    }
    record_audit(database, principal_id, INVOKED_BY, ACTION_PASSWORD, principal_id);
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }
  return {};
}

auto authenticate(db::connection& database, std::string_view email, std::string_view password)
    -> Result<std::string> {
  if (email.empty() || password.empty()) {
    return std::unexpected(credentials_refused());
  }
  if (!random_generator_ready()) {
    return std::unexpected(Error::make(ErrorCode::unavailable, "libsodium did not initialise"));
  }

  auto principal_id = std::string{};
  auto status = std::string{};
  auto hash = std::string{};
  try {
    for (const auto& row :
         database(sqlpp::select(db::principals.id, db::principals.status,
                                db::principals.passwordHash)
                      .from(db::principals)
                      .where(db::principals.email == std::string{email}))) {
      principal_id = std::string{row.id};
      status = std::string{row.status};
      // Copied out of the row: the connector's buffers do not outlive the loop.
      hash = row.passwordHash.has_value() ? std::string{*row.passwordHash} : std::string{};
    }
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }

  if (principal_id.empty() || hash.empty()) {
    return std::unexpected(credentials_refused());
  }
  if (!stored_hash_check(hash, password)) {
    return std::unexpected(credentials_refused());
  }
  if (status != ACTIVE_STATUS) {
    return std::unexpected(Error::make(ErrorCode::permission_denied,
                                       "this account is " + status + "; it cannot sign in"));
  }
  return principal_id;
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
