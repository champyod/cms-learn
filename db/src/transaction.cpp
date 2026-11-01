#include <cms/db/transaction.hpp>

#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>

namespace cms::db {
namespace {

/// How long a commit may take before the caller is told it did not finish. A commit that never completes is
/// indistinguishable from a hung server, and a caller with no bound would wait forever.
constexpr auto COMMIT_TIMEOUT = std::chrono::seconds{30};

/// Hands the commit to the connection's event loop and returns the future it settles.
///
/// Drogon sends COMMIT from that loop once the transaction handle is released, so the callback is registered
/// here, before the release, and the caller waits on the future afterwards.
auto request_commit(::drogon::orm::Transaction& handle) -> std::future<bool> {
  auto outcome = std::make_shared<std::promise<bool>>();
  auto settled = outcome->get_future();
  handle.setCommitCallback([outcome](bool is_committed) { outcome->set_value(is_committed); });
  return settled;
}

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

Transaction::Transaction(::drogon::orm::DbClient& client) : handle_(client.newTransaction()) {
  if (!handle_) {
    throw std::runtime_error("the database refused to open a transaction");
  }
  connection_ = cms::db::connection{std::make_shared<const connection_config>(
      connection_config{.client = handle_})};
}

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
  auto settled = request_commit(*handle_);
  // Both references to the handle must be dropped for the release to happen at all: the connector holds one
  // too, and releasing only this one would keep the transaction open.
  release();
  if (settled.wait_for(COMMIT_TIMEOUT) != std::future_status::ready) {
    throw std::runtime_error("the database did not finish the commit in time");
  }
  if (!settled.get()) {
    throw std::runtime_error("the database refused the commit");
  }
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
