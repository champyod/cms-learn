#include <cms/db/transaction.hpp>

#include <memory>
#include <stdexcept>
#include <utility>

namespace cms::db {

Transaction::Transaction(::drogon::orm::DbClient& client) : handle_(client.newTransaction()) {
  if (!handle_) {
    throw std::runtime_error("the database refused to open a transaction");
  }
  connection_ = cms::db::connection{std::make_shared<const connection_config>(
      connection_config{.client = handle_})};
}

namespace {

/// A destructor must not throw, so a failed rollback is reported as a value rather than propagated.
/// The connection is left to the driver, which discards an open transaction when it closes.
auto roll_back_quietly(const std::shared_ptr<::drogon::orm::Transaction>& handle) noexcept -> bool {
  if (!handle) {
    return false;
  }
  try {
    handle->rollback();
    return true;
  } catch (...) {
    return false;
  }
}

}  // namespace

Transaction::~Transaction() {
  if (!finished_) {
    (void)roll_back_quietly(handle_);
  }
}

auto Transaction::commit() -> void {
  if (finished_) {
    throw std::logic_error("this transaction is already finished");
  }
  finished_ = true;
  // Drogon commits when the handle is released, so both references to it must be dropped: the
  // connector holds one too, and releasing only this one would keep the transaction open.
  release();
}

auto Transaction::rollback() -> void {
  if (finished_) {
    return;
  }
  finished_ = true;
  (void)roll_back_quietly(handle_);
  release();
}

auto Transaction::release() -> void {
  connection_ = cms::db::connection{};
  handle_.reset();
}

}  // namespace cms::db
