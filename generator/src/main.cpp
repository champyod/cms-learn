#include <cms/generate/artefact.hpp>
#include <cms/generate/candidates.hpp>
#include <cms/generate/config.hpp>
#include <cms/generate/pipeline.hpp>
#include <cms/generate/specification.hpp>

#include <array>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <memory>
#include <print>
#include <string>
#include <string_view>

#include <unistd.h>

#include <drogon/orm/DbClient.h>

#include <cms/db/connection.hpp>
#include <cms/object_store.hpp>

namespace {

constexpr int WORK_DONE = 0;
constexpr int WORK_FAILED = 1;
constexpr int CONFIG_REFUSED = 2;
/// The worker's model, database and store are the deployment's, so a value that never answers is reported rather than
/// waited on while a claim sits running.
constexpr double DATABASE_TIMEOUT_SECONDS = 10.0;
/// Every model call of a job shares this seed, and ADR 0015 records it beside each artefact so a completion can be
/// reproduced. The deployer's seed is not yet a record of its own; when it becomes one, this constant is what it replaces.
constexpr std::int64_t MODEL_CALL_SEED = 0;

auto fail(std::string_view message) -> int {
  std::fprintf(stderr, "cms-generate: %.*s\n", static_cast<int>(message.size()), message.data());
  return WORK_FAILED;
}

/// @return A name a person can read after a worker dies without clearing its claim: this host and this process.
auto worker_name() -> std::string {
  auto host = std::array<char, 256>{};
  if (::gethostname(host.data(), host.size()) != 0) {
    return "worker-" + std::to_string(::getpid());
  }
  return std::string{host.data()} + "-" + std::to_string(::getpid());
}

auto client_from(std::string_view database_url) -> std::shared_ptr<::drogon::orm::DbClient> {
  const auto client = ::drogon::orm::DbClient::newPgClient(std::string{database_url}, 1);
  client->setTimeout(DATABASE_TIMEOUT_SECONDS);
  return client;
}

/// Asks the database to answer before anything is claimed: an unreachable one is refused by name, and Drogon's pool is
/// never torn down from its own loop thread, which is what aborts the process when no query ever reached it.
auto database_answers(const std::shared_ptr<::drogon::orm::DbClient>& client) -> bool {
  const auto handle = cms::db::connection_handle{std::make_shared<const cms::db::connection_config>(
      cms::db::connection_config{.client = client})};
  return handle.ping_server();
}

auto run(const cms::generate::ModelConfig& config) -> int {
  const auto client = client_from(config.database_url);
  if (!database_answers(client)) {
    return fail("the database named by CMSLEARN_DATABASE_URL did not answer");
  }
  // The certifier decides a verification on the role that owns that decision, so its pool is a second one, made from
  // its own variable. Reusing the worker's connection would be the privilege escalation the grants exist to prevent.
  const auto certifier_client = client_from(config.certifier_database_url);
  if (!database_answers(certifier_client)) {
    return fail("the database named by CMSLEARN_CERTIFIER_DATABASE_URL did not answer");
  }
  const auto store_config = cms::store::StoreConfig{.endpoint = config.store_endpoint,
                                                    .region = config.store_region,
                                                    .bucket = config.store_bucket,
                                                    .access_key_id = config.store_access_key_id,
                                                    .secret_access_key = config.store_secret_access_key};
  const auto store = cms::store::make_store(store_config);
  if (!store) {
    return fail(store.error().message);
  }

  auto connection = cms::db::connection{std::make_shared<const cms::db::connection_config>(
      cms::db::connection_config{.client = client})};
  auto candidate_sink = cms::generate::SqlCandidateSink{*client};
  auto specification_sink = cms::generate::SqlSpecificationSink{*client, **store};
  auto artefact_sink = cms::generate::SqlArtefactSink{*client, **store};
  auto statement_sink = cms::generate::SqlStatementSink{*client, **store};
  auto solution_sink = cms::generate::SqlSolutionSink{*client, **store};
  auto test_plan_sink = cms::generate::SqlTestPlanSink{*client, **store};
  auto certifier = cms::generate::SqlCertifier{*certifier_client, **store, config.commit_id};
  const auto ports = cms::generate::PipelinePorts{.client = *client,
                                                  .store = **store,
                                                  .candidates = candidate_sink,
                                                  .specifications = specification_sink,
                                                  .artefacts = artefact_sink,
                                                  .statements = statement_sink,
                                                  .solutions = solution_sink,
                                                  .test_plans = test_plan_sink,
                                                  .certifier = certifier};
  // The vocabulary and the locale names are content (ADR 0056), read from the root the deployment names, so no
  // language is decided in this binary.
  const auto facts = cms::generate::RunnerFacts{.content_root = std::filesystem::path{config.content_root}};
  // The model is the operator's record, not the environment: the client reads the effective record on its first call,
  // opens the key with the deployment's key and picks the adapter from the record's protocol. Reading at the call means a
  // disabled record fails the step by name and ends the job failed — the operator fixes the record, not the process.
  const auto model_config = std::make_shared<const cms::db::connection_config>(
      cms::db::connection_config{.client = client});
  auto model = cms::generate::RecordBackedClient{model_config, std::string{cms::generate::PROMPT_VERSION},
                                                  MODEL_CALL_SEED};

  const auto report = cms::generate::run_pipeline(model, connection, worker_name(), ports, facts);
  if (!report) {
    return fail(report.error().message);
  }
  if (report->job_id == 0) {
    std::println("no generation job is claimable");
    return WORK_DONE;
  }
  std::println("generation job {} ended {}", report->job_id, report->state);
  return WORK_DONE;
}

/// The generation worker: it proves its configuration is the deployer's, then claims one queued job and drives it
/// through the steps this build wires (1 to 3). A missing value names the key and never prints one.
auto worker(int argc, char** argv) -> int {
  const auto check_only = argc > 1 && std::string{argv[1]} == "--check";
  const auto config = cms::generate::read_model_config(cms::generate::environment_lookup());
  if (!config) {
    std::fprintf(stderr, "cms-generate: %s\n", config.error().message.c_str());
    return CONFIG_REFUSED;
  }
  if (check_only) {
    return WORK_DONE;
  }
  return run(*config);
}

}  // namespace

// An exception leaving main ends the process through std::terminate with no message an operator can act on;
// catching it here reports the same line the error paths do.
auto main(int argc, char** argv) -> int {
  try {
    return worker(argc, argv);
  } catch (const std::exception& error) {
    return fail(error.what());
  }
}
