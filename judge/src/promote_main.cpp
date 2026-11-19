// Publishes one draft task, on the certificate row the certifier wrote for its version.
//
// The promotion is the only path from draft to published, so it carries the checks: a task whose certificate
// is missing or does not say verified is refused before anything is read, and a reference that does not
// reproduce its expected output stops it afterwards. The connection is the deployer's, named by the same
// variable the judge's run path reads.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <memory>
#include <print>
#include <string>
#include <string_view>

#include <drogon/orm/DbClient.h>

#include <cms/db/connection.hpp>
#include <cms/judge/promote.hpp>

namespace {

constexpr const char* DATABASE_VARIABLE = "CMSLEARN_DATABASE_URL";
/// A publish is decided in one process, so a database that never answers is refused rather than waited on.
constexpr double DATABASE_TIMEOUT_SECONDS = 10.0;
constexpr std::string_view USAGE =
    "usage: cms_promote_task <task-dir> <artifact-dir> <reference-exe>";

auto fail(std::string_view message) -> int {
  std::fprintf(stderr, "cms-promote-task: %.*s\n", static_cast<int>(message.size()), message.data());
  return 1;
}

auto client_from_environment() -> std::shared_ptr<::drogon::orm::DbClient> {
  const auto* dsn = std::getenv(DATABASE_VARIABLE);
  if (dsn == nullptr || std::strlen(dsn) == 0) {
    return nullptr;
  }
  const auto client = ::drogon::orm::DbClient::newPgClient(std::string{dsn}, 1);
  client->setTimeout(DATABASE_TIMEOUT_SECONDS);
  return client;
}

/// Asks the database to answer before anything is decided: an unreachable one is refused by name, and Drogon's pool
/// is never torn down from its own loop thread, which is what aborts the process when no query ever reached it.
auto database_answers(const std::shared_ptr<::drogon::orm::DbClient>& client) -> bool {
  const auto handle = cms::db::connection_handle{std::make_shared<const cms::db::connection_config>(
      cms::db::connection_config{.client = client})};
  return handle.ping_server();
}

auto run(int argc, char** argv) -> int {
  if (argc != 4) {
    return fail(USAGE);
  }
  const auto client = client_from_environment();
  if (client == nullptr) {
    return fail(std::string{"a publish reads its certificate row, so "} + DATABASE_VARIABLE +
                " must name the database");
  }
  if (!database_answers(client)) {
    return fail(std::string{"the database named by "} + DATABASE_VARIABLE + " did not answer");
  }
  const auto certificates = cms::judge::SqlCertificateSource{client};
  const auto promoted = cms::judge::promote_task(std::filesystem::path{argv[1]},
                                                 std::filesystem::path{argv[2]},
                                                 std::filesystem::path{argv[3]}, certificates);
  if (!promoted) {
    return fail(promoted.error().message);
  }
  std::println("published {} -> {}", promoted->digest, promoted->artifact.string());
  return 0;
}

}  // namespace

// An exception leaving main ends the process through std::terminate with no message an operator can act on;
// catching it here reports the same line the error paths do.
int main(int argc, char** argv) {
  try {
    return run(argc, argv);
  } catch (const std::exception& error) {
    return fail(error.what());
  }
}
