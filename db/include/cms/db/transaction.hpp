#pragma once

#include <drogon/orm/DbClient.h>
#include <memory>

#include <cms/db/connection.hpp>

namespace cms::db {

/// @brief A transaction that rolls back unless it is told to commit.
///
/// Drogon commits a transaction when its handle is released, which makes "forgot to roll back" a
/// silent commit. This type inverts that: the destructor rolls back, and commit() is the only path
/// that lets the implicit commit happen. A failure therefore cannot leave a half-written row behind,
/// because leaving the scope is itself the rollback.
class Transaction {
 public:
  /// @brief Opens a transaction on @p client. Throws when the database refuses one.
  explicit Transaction(::drogon::orm::DbClient& client);

  ~Transaction();

  Transaction(const Transaction&) = delete;
  Transaction& operator=(const Transaction&) = delete;
  Transaction(Transaction&&) = delete;
  Transaction& operator=(Transaction&&) = delete;

  /// @brief The connector bound to this transaction; every statement must run through it.
  [[nodiscard]] auto connection() -> cms::db::connection& { return connection_; }

  /// @brief Lets the transaction commit when its handle is released.
  /// @pre Neither commit() nor rollback() has been called.
  void commit();

  /// @brief Rolls back now. Called by the destructor when commit() was not.
  void rollback();

 private:
  void release();

  std::shared_ptr<::drogon::orm::Transaction> handle_;
  cms::db::connection connection_;
  bool finished_{false};
};

}  // namespace cms::db
