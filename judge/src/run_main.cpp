// Claims one submission, runs it in the sandbox, writes the verdict back, and prints it.
//
// One invocation does one row: the claim is the lease, and a run that cannot finish leaves the row claimed
// so the lease returns it to the queue rather than writing a verdict nobody earned.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <vector>

#include <drogon/orm/DbClient.h>
#include <sqlpp23/sqlpp23.h>

#include <cms/db/connection.hpp>
#include <cms/db/tables.hpp>
#include <cms/judge/run.hpp>
#include <cms/judge/worker.hpp>

namespace {

constexpr const char* DATABASE_VARIABLE = "CMSLEARN_DATABASE_URL";
constexpr std::string_view USAGE =
    "usage: cms_judge_run --isolate <path> --checker <path> [--compiler <path>] [--worker <name>] "
    "[--box-id <id>] [--standard <std>]";

auto fail(std::string_view message) noexcept -> int {
  try {
    std::println(stderr, "cms-judge-run: {}", message);
  } catch (...) {
    constexpr std::string_view PREFIX = "cms-judge-run: ";
    std::fwrite(PREFIX.data(), 1, PREFIX.size(), stderr);
    std::fwrite(message.data(), 1, message.size(), stderr);
    std::fputc('\n', stderr);
  }
  return 1;
}

struct Flags {
  std::string isolate;
  std::string checker;
  std::string compiler{"g++"};
  std::string worker{"judge-run"};
  std::string box_id{"0"};
  std::string standard{std::string{cms::judge::DEFAULT_CPP_STANDARD}};
};

auto parse_flags(int argc, char** argv) -> std::optional<Flags> {
  auto flags = Flags{};
  for (auto index = 1; index < argc; index += 2) {
    if (index + 1 >= argc) {
      return std::nullopt;
    }
    const auto name = std::string_view{argv[index]};
    const auto value = std::string_view{argv[index + 1]};
    if (name == "--isolate") {
      flags.isolate = value;
    } else if (name == "--checker") {
      flags.checker = value;
    } else if (name == "--compiler") {
      flags.compiler = value;
    } else if (name == "--worker") {
      flags.worker = value;
    } else if (name == "--box-id") {
      flags.box_id = value;
    } else if (name == "--standard") {
      flags.standard = value;
    } else {
      return std::nullopt;
    }
  }
  if (flags.isolate.empty() || flags.checker.empty()) {
    return std::nullopt;
  }
  return flags;
}

auto database_from_environment() -> std::optional<cms::db::connection> {
  const auto* dsn = std::getenv(DATABASE_VARIABLE);
  if (dsn == nullptr || std::strlen(dsn) == 0) {
    return std::nullopt;
  }
  static const auto client = ::drogon::orm::DbClient::newPgClient(std::string{dsn}, 1);
  return cms::db::connection{std::make_shared<const cms::db::connection_config>(
      cms::db::connection_config{.client = client})};
}

auto load_task(cms::db::connection& database, const cms::judge::ClaimedSubmission& submission,
               const Flags& flags) -> cms::Result<cms::judge::TaskRun> {
  auto task = cms::judge::TaskRun{};
  auto found = false;
  for (const auto& row :
       database(sqlpp::select(cms::db::tasks.timeLimitMs, cms::db::tasks.memoryLimitMb,
                             cms::db::tasks.checker)
                    .from(cms::db::tasks)
                    .where(cms::db::tasks.id == submission.task_id &&
                           cms::db::tasks.version == submission.task_version))) {
    found = true;
    const auto kind = std::string{row.checker};
    if (kind != "token") {
      return std::unexpected(cms::Error::make(
          cms::ErrorCode::invalid_argument,
          "the judge does not run the '" + kind + "' checker kind yet; the row stays claimable"));
    }
    task.time_limit_ms = row.timeLimitMs;
    task.memory_limit_kb = row.memoryLimitMb * 1024;
    task.checker = flags.checker;
    task.cpp_standard = flags.standard;
  }
  if (!found) {
    return std::unexpected(cms::Error::make(
        cms::ErrorCode::not_found, "no task row for " + submission.task_id + " version " +
                                       std::to_string(submission.task_version)));
  }

  for (const auto& row :
       database(sqlpp::select(cms::db::taskExamples.input, cms::db::taskExamples.expectedOutput)
                    .from(cms::db::taskExamples)
                    .where(cms::db::taskExamples.taskId == submission.task_id &&
                           cms::db::taskExamples.taskVersion == submission.task_version)
                    .order_by(cms::db::taskExamples.ordinal.asc()))) {
    task.cases.push_back(cms::judge::Case{.input = std::string{row.input},
                                          .expected_output = std::string{row.expectedOutput}});
  }
  if (task.cases.empty()) {
    return std::unexpected(cms::Error::make(
        cms::ErrorCode::invalid_argument,
        "the task has no examples; a submission cannot be judged against nothing"));
  }
  return task;
}

auto run(int argc, char** argv) -> int {
  const auto flags = parse_flags(argc, argv);
  if (!flags) {
    return fail(USAGE);
  }
  auto database = database_from_environment();
  if (!database) {
    return fail("CMSLEARN_DATABASE_URL is not set; the runner claims its work from it");
  }

  auto claimed = cms::judge::claim_submission(*database, flags->worker);
  if (!claimed) {
    return fail(claimed.error().message);
  }
  if (!claimed->has_value()) {
    std::println("no claimable submission");
    return 0;
  }
  const auto& submission = **claimed;

  const auto task = load_task(*database, submission, *flags);
  if (!task) {
    return fail(task.error().message);
  }
  const auto settings = cms::judge::RunSettings{.isolate = flags->isolate,
                                                .compiler = flags->compiler,
                                                .box_id = flags->box_id};
  const auto judgement = cms::judge::judge_submission(settings, *task, submission.source);
  if (!judgement) {
    return fail(judgement.error().message);
  }
  const auto finished = cms::judge::finish_submission(*database, submission.id, *judgement);
  if (!finished) {
    return fail(finished.error().message);
  }
  std::println("submission {} {}: verdict {} ({})", submission.id, submission.task_id,
               cms::judge::submission_verdict_name(judgement->verdict), judgement->detail);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    return run(argc, argv);
  } catch (const std::exception& error) {
    return fail(error.what());
  } catch (...) {
    return fail("the run stopped on a failure that is not a std::exception");
  }
}
