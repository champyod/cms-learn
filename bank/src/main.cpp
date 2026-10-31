// Imports the task bank into the database.
//
// The bank is files; this is the only thing that turns them into rows. It runs to completion and exits,
// so the image can seed on start without a long-lived process holding a transaction open.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <print>
#include <string>
#include <string_view>

#include <drogon/orm/DbClient.h>

#include <cms/bank/bank.hpp>
#include <cms/bank/seed.hpp>
#include <cms/db/connection.hpp>
#include <cms/db/transaction.hpp>

namespace {

constexpr std::size_t SEED_CONNECTIONS = 1;
constexpr std::string_view USAGE = "usage: cms_seed_content --content <directory>";
/// A C string because getenv needs a terminator; a string_view does not promise one.
constexpr const char* DATABASE_VARIABLE = "CMSLEARN_DATABASE_URL";

auto fail(std::string_view message) -> int {
  try {
    std::println(stderr, "cms-seed-content: {}", message);
  } catch (...) {
    constexpr std::string_view PREFIX = "cms-seed-content: ";
    std::fwrite(PREFIX.data(), 1, PREFIX.size(), stderr);
    std::fwrite(message.data(), 1, message.size(), stderr);
    std::fputc('\n', stderr);
  }
  return 1;
}

auto content_root(int argc, char** argv) -> std::string {
  for (auto index = 1; index < argc; ++index) {
    if (std::string_view{argv[index]} == "--content" && index + 1 < argc) {
      return argv[index + 1];
    }
  }
  return {};
}

/// Drogon gives a DbClient no public shutdown: its event-loop thread outlives every reference a caller
/// can drop. Holding it for the process lifetime keeps it reachable at exit, which is what it is, rather
/// than letting the driver's own allocations surface as a leak this program caused.
auto shared_client(const char* dsn) -> const std::shared_ptr<::drogon::orm::DbClient>& {
  static const auto client = ::drogon::orm::DbClient::newPgClient(dsn, SEED_CONNECTIONS);
  return client;
}

auto run(int argc, char** argv) -> int {
  const auto root = content_root(argc, argv);
  if (root.empty()) {
    return fail(USAGE);
  }
  const auto* dsn = std::getenv(DATABASE_VARIABLE);
  if (dsn == nullptr || std::strlen(dsn) == 0) {
    return fail(std::string{DATABASE_VARIABLE} + " is not set; this job takes its address from it");
  }

  const auto bank = cms::bank::load(std::filesystem::path{root});
  if (!bank) {
    return fail(bank.error().message);
  }

  const auto config = std::make_shared<const cms::db::connection_config>(
      cms::db::connection_config{.client = shared_client(dsn)});
  auto transaction = cms::db::Transaction{*config->client};

  const auto report = cms::bank::seed(transaction.connection(), *bank);
  if (!report) {
    return fail(report.error().message);
  }
  transaction.commit();
  std::println("content version {} ({}) — {} topic(s), {} inserted, {} updated, {} kept",
               report->version, report->is_changed ? "changed" : "unchanged",
               report->topics_written, report->tasks_inserted, report->tasks_updated,
               report->tasks_kept);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  auto status = 1;
  try {
    status = run(argc, argv);
  } catch (const std::exception& error) {
    status = fail(error.what());
  } catch (...) {
    status = fail("the import stopped on a failure that is not a std::exception");
  }

  // Drogon gives a client no way to join its event-loop threads: the pool outlives main, and every
  // sanitizer build then reports the driver's own bookkeeping as a leak this program caused. The import
  // is finished and its transaction is committed, so the process ends here without running destructors —
  // the kernel returns the memory, and the exit status still says what happened.
  std::fflush(nullptr);
  std::_Exit(status);
}
