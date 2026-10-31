#include <cms/judge/supervisor.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

auto stub_named(std::string_view name) -> std::string {
  return std::string{CMS_STUB_DIR} + "/" + std::string{name};
}

/// A box directory that exists, because the supervisor passes paths into it and a missing parent would
/// make the stub's own file write fail for the wrong reason. Each case names its own, so a report one case
/// wrote cannot be read by the next.
auto scratch_box(std::string_view name) -> std::filesystem::path {
  const auto directory = std::filesystem::temp_directory_path() / ("cms-judge-box-" + std::string{name});
  std::filesystem::create_directories(directory);
  return directory;
}

auto spec_with_wall_limit(const std::filesystem::path& box, std::int64_t milliseconds)
    -> cms::judge::RunSpec {
  auto spec = cms::judge::RunSpec{};
  spec.box_id = "3";
  spec.box_dir = box;
  spec.wall_limit_ms = milliseconds;
  return spec;
}

}  // namespace

TEST_CASE("the isolate command line is the one the v2.7 manual documents", "[supervisor][unit]") {
  const auto box = std::filesystem::path{"/var/lib/isolate/3/box"};
  const auto arguments = cms::judge::isolate_arguments(spec_with_wall_limit(box, 10000));

  const auto expected = std::vector<std::string>{
      "--box-id",    "3",
      "--meta",      "/var/lib/isolate/3/box/meta.txt",
      "--stdin",     "/var/lib/isolate/3/box/input.txt",
      "--stdout",    "/var/lib/isolate/3/box/output.txt",
      "--stderr",    "/var/lib/isolate/3/box/error.txt",
      "--time",      "1.000",
      "--wall-time", "10.000",
      "--mem",       "262144",
      "--processes", "1",
      "--dir",       "box=/var/lib/isolate/3/box",
  };
  CHECK(arguments == expected);
}

TEST_CASE("a sandbox that ignores SIGTERM is escalated to SIGKILL", "[supervisor][unit]") {
  // The plan's E5.2 falsification in miniature: remove the escalation and this case hangs until the test
  // runner's own timeout. The stub traps TERM, so only SIGKILL ends it.
  const auto started = std::chrono::steady_clock::now();
  const auto outcome = cms::judge::run_sandboxed(spec_with_wall_limit(scratch_box("stubborn"), 300),
                                                 stub_named("stubborn-isolate.sh"));
  const auto elapsed = std::chrono::steady_clock::now() - started;

  REQUIRE(outcome.has_value());
  CHECK(outcome->is_killed);
  CHECK(elapsed < std::chrono::seconds{5});
}

TEST_CASE("the report is read from the meta file, not from the exit code", "[supervisor][unit]") {
  const auto outcome = cms::judge::run_sandboxed(spec_with_wall_limit(scratch_box("cooperative"), 2000),
                                                 stub_named("cooperative-isolate.sh"));
  REQUIRE(outcome.has_value());
  CHECK(!outcome->is_killed);
  CHECK(outcome->status == "OK");
  CHECK(outcome->meta.at("max-rss") == "1024");
}

TEST_CASE("a sandbox that reports nothing is an error, not a verdict", "[supervisor][unit]") {
  const auto outcome = cms::judge::run_sandboxed(spec_with_wall_limit(scratch_box("silent"), 2000),
                                                 stub_named("silent-checker.sh"));
  // The stub writes no meta file at all, so the supervisor must refuse rather than report an empty run.
  REQUIRE(!outcome.has_value());
  CHECK(outcome.error().code == cms::ErrorCode::unavailable);
}

TEST_CASE("a box that cannot be initialised is reported as such", "[supervisor][unit]") {
  const auto prepared = cms::judge::prepare_box("3", stub_named("garbage-checker.sh"));
  REQUIRE(!prepared.has_value());
  CHECK(prepared.error().code == cms::ErrorCode::unavailable);
}

TEST_CASE("a fork bomb is contained by the sandbox and torn down", "[supervisor][isolate]") {
  // The plan's E5.2 gate. It needs isolate on the host plus root and cgroups, so it skips by name rather
  // than pretending to pass: the moment the binary exists this becomes a measurement, with no code change.
  const auto isolate = std::string{"/usr/bin/isolate"};
  if (!std::filesystem::exists(isolate)) {
    SKIP("isolate is not installed; this case needs it with root and cgroups");
  }

  const auto box = cms::judge::prepare_box("7", isolate);
  REQUIRE(box.has_value());

  {
    auto program = std::ofstream{*box / "bomb.sh"};
    program << "#!/bin/sh\n"
               ":(){ :|:& };:\n"
               "sleep 60\n";
  }
  std::filesystem::permissions(*box / "bomb.sh",
                               std::filesystem::perms::owner_exec |
                                   std::filesystem::perms::group_exec,
                               std::filesystem::perm_options::add);

  auto spec = cms::judge::RunSpec{};
  spec.box_id = "7";
  spec.box_dir = *box;
  spec.program = "./bomb.sh";
  spec.time_limit_ms = 500;
  spec.wall_limit_ms = 3000;
  spec.process_limit = 1;

  const auto outcome = cms::judge::run_sandboxed(spec, isolate);
  const auto cleaned = cms::judge::discard_box("7", isolate);
  CHECK(cleaned.has_value());

  REQUIRE(outcome.has_value());
  // One process is allowed, so the fork fails inside the sandbox: the report says the program was killed
  // by a signal rather than that the box leaked a process tree.
  CHECK((outcome->status == "SG" || outcome->status == "TO" || outcome->is_killed));
}
