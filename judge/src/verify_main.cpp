// Verifies one task's cases against its reference solution and writes the manifest the judge reads.
//
// The same binary is what CI runs and what an author runs before committing a task: one code path, so a
// manifest can never describe cases that were checked by a different rule than the judge applies.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <print>
#include <string>
#include <string_view>

#include <cms/judge/manifest.hpp>
#include <cms/judge/verify.hpp>

namespace {

constexpr std::string_view USAGE = "usage: cms_verify_task <task-dir> <reference-exe> [--write]";

auto fail(std::string_view message) -> int {
  try {
    std::println(stderr, "cms-verify-task: {}", message);
  } catch (...) {
    constexpr std::string_view PREFIX = "cms-verify-task: ";
    std::fwrite(PREFIX.data(), 1, PREFIX.size(), stderr);
    std::fwrite(message.data(), 1, message.size(), stderr);
    std::fputc('\n', stderr);
  }
  return 1;
}

auto has_flag(int argc, char** argv, std::string_view flag) -> bool {
  for (auto index = 3; index < argc; ++index) {
    if (std::string_view{argv[index]} == flag) {
      return true;
    }
  }
  return false;
}

auto run(int argc, char** argv) -> int {
  if (argc < 3) {
    return fail(USAGE);
  }
  const auto task_dir = std::filesystem::path{argv[1]};
  const auto reference = std::filesystem::path{argv[2]};

  const auto manifest = cms::judge::verify_task(task_dir, reference);
  if (!manifest) {
    return fail(manifest.error().message);
  }

  if (has_flag(argc, argv, "--write")) {
    const auto written =
        cms::judge::write_manifest(task_dir / "tests" / "manifest.json", *manifest);
    if (!written) {
      return fail(written.error().message);
    }
    std::println("{} verified: {} case(s)", manifest->task, manifest->cases.size());
    return 0;
  }
  std::print("{}", cms::judge::render_manifest(*manifest));
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    return run(argc, argv);
  } catch (const std::exception& error) {
    return fail(error.what());
  } catch (...) {
    return fail("verification stopped on a failure that is not a std::exception");
  }
}
